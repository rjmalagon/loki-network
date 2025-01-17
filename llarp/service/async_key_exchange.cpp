#include <service/async_key_exchange.hpp>

#include <crypto/crypto.hpp>
#include <crypto/types.hpp>
#include <util/logic.hpp>
#include <util/memfn.hpp>

namespace llarp
{
  namespace service
  {
    AsyncKeyExchange::AsyncKeyExchange(std::shared_ptr< Logic > l,
                                       const ServiceInfo& r,
                                       const Identity& localident,
                                       const PQPubKey& introsetPubKey,
                                       const Introduction& remote,
                                       IDataHandler* h, const ConvoTag& t,
                                       ProtocolType proto)
        : logic(l)
        , remote(r)
        , m_LocalIdentity(localident)
        , introPubKey(introsetPubKey)
        , remoteIntro(remote)
        , handler(h)
        , tag(t)
    {
      msg.proto = proto;
    }

    void
    AsyncKeyExchange::Result(void* user)
    {
      AsyncKeyExchange* self = static_cast< AsyncKeyExchange* >(user);
      // put values
      self->handler->PutSenderFor(self->msg.tag, self->remote, false);
      self->handler->PutCachedSessionKeyFor(self->msg.tag, self->sharedKey);
      self->handler->PutIntroFor(self->msg.tag, self->remoteIntro);
      self->handler->PutReplyIntroFor(self->msg.tag, self->msg.introReply);
      self->hook(self->frame);
      delete self;
    }

    void
    AsyncKeyExchange::Encrypt(void* user)
    {
      AsyncKeyExchange* self = static_cast< AsyncKeyExchange* >(user);
      // derive ntru session key component
      SharedSecret K;
      auto crypto = CryptoManager::instance();
      crypto->pqe_encrypt(self->frame.C, K, self->introPubKey);
      // randomize Nonce
      self->frame.N.Randomize();
      // compure post handshake session key
      // PKE (A, B, N)
      SharedSecret sharedSecret;
      path_dh_func dh_client = util::memFn(&Crypto::dh_client, crypto);
      if(!self->m_LocalIdentity.KeyExchange(dh_client, sharedSecret,
                                            self->remote, self->frame.N))
      {
        LogError("failed to derive x25519 shared key component");
      }
      std::array< byte_t, 64 > tmp = {{0}};
      // K
      std::copy(K.begin(), K.end(), tmp.begin());
      // H (K + PKE(A, B, N))
      std::copy(sharedSecret.begin(), sharedSecret.end(), tmp.begin() + 32);
      crypto->shorthash(self->sharedKey, llarp_buffer_t(tmp));
      // set tag
      self->msg.tag = self->tag;
      // set sender
      self->msg.sender = self->m_LocalIdentity.pub;
      // set version
      self->msg.version = LLARP_PROTO_VERSION;
      // encrypt and sign
      if(self->frame.EncryptAndSign(self->msg, K, self->m_LocalIdentity))
        self->logic->queue_job({self, &Result});
      else
      {
        LogError("failed to encrypt and sign");
        delete self;
      }
    }
  }  // namespace service
}  // namespace llarp
