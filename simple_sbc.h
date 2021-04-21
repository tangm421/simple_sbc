
#if !defined(SIPSVR__H)
#define SIPSVR__H

#include "rutil/ServerProcess.hxx"
#include "resip/dum/Handles.hxx"
#include "resip/dum/RegistrationHandler.hxx"
#include "resip/dum/InviteSessionHandler.hxx"
#include "resip/dum/Handle.hxx"
#include "resip/dum/AppDialog.hxx"
#include "resip/dum/AppDialogSet.hxx"
#include "resip/dum/AppDialogSetFactory.hxx"
#include "resip/dum/UserProfile.hxx"
#include "resip/dum/MasterProfile.hxx"
#include "resip/dum/DialogSetHandler.hxx"
#include "resip/dum/InMemorySyncRegDb.hxx"
#include "resip/stack/SipMessage.hxx"

#include "cmd_option.h"


namespace resip
{
    class SipStack;
    class DialogUsageManager;
    class ThreadIf;
    class RegistrationPersistenceManager;
}

// Used to set the IP Address in outbound SDP to match the IP address choosen by the stack to send the message on
class SdpMessageDecorator : public resip::MessageDecorator
{
public:
    virtual ~SdpMessageDecorator() {}
    virtual void decorateMessage(resip::SipMessage& msg,
        const resip::Tuple& source,
        const resip::Tuple& destination,
        const resip::Data& sigcompId)
    {
        resip::SdpContents* sdp = dynamic_cast<resip::SdpContents*>(msg.getContents());
        if (sdp)
        {
            // Fill in IP and Port from source
            sdp->session().origin().setAddress(resip::Tuple::inet_ntop(source), source.ipVersion() == resip::V6 ? resip::SdpContents::IP6 : resip::SdpContents::IP4);
            sdp->session().connection().setAddress(resip::Tuple::inet_ntop(source), source.ipVersion() == resip::V6 ? resip::SdpContents::IP6 : resip::SdpContents::IP4);
        }
    }
    virtual void rollbackMessage(resip::SipMessage& msg) {}  // Nothing to do
    virtual resip::MessageDecorator* clone() const { return new SdpMessageDecorator; }
};

class SSDialogSet;
class SimpleSBC
    : public resip::ServerProcess
    , public resip::InMemorySyncRegDbHandler
    , public resip::ServerRegistrationHandler
    , public resip::InviteSessionHandler
    , public resip::DialogSetHandler
{
public:
    using SipMessage = resip::SipMessage;
    using SdpContents = resip::SdpContents;
    using ClientRegistrationHandle = resip::ClientRegistrationHandle;
    using ServerRegistrationHandle = resip::ServerRegistrationHandle;
    using ClientInviteSessionHandle = resip::ClientInviteSessionHandle;
    using ServerInviteSessionHandle = resip::ServerInviteSessionHandle;
    using InviteSessionHandle = resip::InviteSessionHandle;
    using InviteSession = resip::InviteSession;
    using ClientSubscriptionHandle = resip::ClientSubscriptionHandle;
    using ServerSubscriptionHandle = resip::ServerSubscriptionHandle;
    using UserProfile = resip::UserProfile;

    class AorContact
    {
    public:
        AorContact(const resip::Uri& aor, const resip::ContactList* contacts)
            : mAor(aor), mContacts(contacts) {}
        bool operator==(const AorContact* ac) {
            return mAor == ac->mAor;
        }
        resip::Uri mAor;
        const resip::ContactList* mContacts;
    };

    SimpleSBC();
    ~SimpleSBC();

    bool startup(std::unique_ptr<CmdRunner> cmd);
    void shutdown();

    bool makeNewCall(const resip::Uri& aor, const resip::Data& sdpfile);
    bool makeNewCall(UInt64 id, const resip::Data& sdpfile);
    void finishCall(const std::list<UInt64>& ids);
    bool makeReinvite(UInt64 id, const resip::Data& sdpfile);
    void showAllReg();
    void showAllCall();

    resip::DialogUsageManager& getDialogUsageManager() { return *mDum; }

protected:
    //////////////////////////////////////////////////////////////////////////
    friend class SSDialogSet;

    const resip::Data& getSdpFile() const { return resip::Data::Empty; }

    bool createSipStack();
    bool createDialogUsageManager();
    bool createRegistrationManager();

    void addDomains(resip::TransactionUser& tu);
    bool addTransports();

    // InMemorySyncRegDbHandler ////////////////////////////////////////////////////////////////////////
    void onAorModified(const resip::Uri& aor, const resip::ContactList& contacts);

    // Server Registration Handler ////////////////////////////////////////////////////////////////////////
    /// Called when registration is refreshed
    virtual void onRefresh(ServerRegistrationHandle, const SipMessage& reg);
    /// called when one or more specified contacts is removed
    virtual void onRemove(ServerRegistrationHandle, const SipMessage& reg);
    /// Called when all the contacts are removed using "Contact: *"
    virtual void onRemoveAll(ServerRegistrationHandle, const SipMessage& reg);
    /** Called when one or more contacts are added. This is after
        authentication has all succeeded */
    virtual void onAdd(ServerRegistrationHandle, const SipMessage& reg);
    /// Called when a client queries for the list of current registrations
    virtual void onQuery(ServerRegistrationHandle, const SipMessage& reg);

    // Invite Session Handler ////////////////////////////////////////////////////////////////////////
    /// called when an initial INVITE or the intial response to an outoing invite  
    virtual void onNewSession(ClientInviteSessionHandle, InviteSession::OfferAnswerType oat, const SipMessage& msg);
    virtual void onNewSession(ServerInviteSessionHandle, InviteSession::OfferAnswerType oat, const SipMessage& msg) {}
    /// Received a failure response from UAS
    virtual void onFailure(ClientInviteSessionHandle, const SipMessage& msg) {}
    /// called when an in-dialog provisional response is received that contains a body
    virtual void onEarlyMedia(ClientInviteSessionHandle, const SipMessage&, const SdpContents&) {}
    /// called when dialog enters the Early state - typically after getting 18x
    virtual void onProvisional(ClientInviteSessionHandle, const SipMessage&);
    virtual void onConnected(ClientInviteSessionHandle, const SipMessage& msg);
    /// called when a dialog initiated as a UAS enters the connected state
    virtual void onConnected(InviteSessionHandle, const SipMessage& msg) {}
    /// called when ACK (with out an answer) is received for initial invite (UAS)
    virtual void onConnectedConfirmed(InviteSessionHandle, const SipMessage &msg) {}
    /// called when PRACK is received for a reliable provisional answer (UAS)
    virtual void onPrack(ServerInviteSessionHandle, const SipMessage &msg) {}
    /** UAC gets no final response within the stale call timeout (default is 3
     * minutes). This is just a notification. After the notification is
     * called, the InviteSession will then call
     * InviteSessionHandler::terminate() */
    virtual void onStaleCallTimeout(ClientInviteSessionHandle h) {}
    /** called when an early dialog decides it wants to terminate the
     * dialog. Default behavior is to CANCEL all related early dialogs as
     * well.  */
    virtual void terminate(ClientInviteSessionHandle h) {}
    virtual void onTerminated(InviteSessionHandle, InviteSessionHandler::TerminatedReason reason, const SipMessage* related = 0);
    /// called when a fork that was created through a 1xx never receives a 2xx
    /// because another fork answered and this fork was canceled by a proxy. 
    virtual void onForkDestroyed(ClientInviteSessionHandle) {}
    /// called when a 3xx with valid targets is encountered in an early dialog     
    /// This is different then getting a 3xx in onTerminated, as another
    /// request will be attempted, so the DialogSet will not be destroyed.
    /// Basically an onTermintated that conveys more information.
    /// checking for 3xx respones in onTerminated will not work as there may
    /// be no valid targets.
    virtual void onRedirected(ClientInviteSessionHandle, const SipMessage& msg) {}
    /// called to allow app to adorn a message. default is to send immediately
    virtual void onReadyToSend(InviteSessionHandle, SipMessage& msg) {}
    /// called when an answer is received - has nothing to do with user
    /// answering the call 
    virtual void onAnswer(InviteSessionHandle, const SipMessage& msg, const SdpContents&);
    /// called when an offer is received - must send an answer soon after this
    virtual void onOffer(InviteSessionHandle, const SipMessage& msg, const SdpContents&) {}
    /// called when a modified body is received in a 2xx response to a
    /// session-timer reINVITE. Under normal circumstances where the response
    /// body is unchanged from current remote body no handler is called
    virtual void onRemoteSdpChanged(InviteSessionHandle, const SipMessage& msg, const SdpContents&);
    /// Called when an error response is received for a reinvite-nobody request (via requestOffer)
    virtual void onOfferRequestRejected(InviteSessionHandle, const SipMessage& msg);
    /// called when an Invite w/out offer is sent, or any other context which
    /// requires an offer from the user
    virtual void onOfferRequired(InviteSessionHandle, const SipMessage& msg) {}
    /// called if an offer in a UPDATE or re-INVITE was rejected - not real
    /// useful. A SipMessage is provided if one is available
    virtual void onOfferRejected(InviteSessionHandle, const SipMessage* msg) {}
    /// called when INFO message is received 
    /// the application must call acceptNIT() or rejectNIT()
    /// once it is ready for another message.
    virtual void onInfo(InviteSessionHandle, const SipMessage& msg) {}
    /// called when response to INFO message is received 
    virtual void onInfoSuccess(InviteSessionHandle, const SipMessage& msg) {}
    virtual void onInfoFailure(InviteSessionHandle, const SipMessage& msg) {}
    /// called when MESSAGE message is received 
    virtual void onMessage(InviteSessionHandle, const SipMessage& msg) {}
    /// called when response to MESSAGE message is received 
    virtual void onMessageSuccess(InviteSessionHandle, const SipMessage& msg) {}
    virtual void onMessageFailure(InviteSessionHandle, const SipMessage& msg) {}
    /// called when an REFER message is received.  The refer is accepted or
    /// rejected using the server subscription. If the offer is accepted,
    /// DialogUsageManager::makeInviteSessionFromRefer can be used to create an
    /// InviteSession that will send notify messages using the ServerSubscription
    virtual void onRefer(InviteSessionHandle, ServerSubscriptionHandle, const SipMessage& msg) {}
    virtual void onReferNoSub(InviteSessionHandle, const SipMessage& msg) {}
    /// called when an REFER message receives a failure response 
    virtual void onReferRejected(InviteSessionHandle, const SipMessage& msg) {}
    /// called when an REFER message receives an accepted response 
    virtual void onReferAccepted(InviteSessionHandle, ClientSubscriptionHandle, const SipMessage& msg) {}

    // DialogSetHandler //////////////////////////////////////////////
    virtual void onTrying(resip::AppDialogSetHandle, const resip::SipMessage& msg);
    virtual void onNonDialogCreatingProvisional(resip::AppDialogSetHandle, const resip::SipMessage& msg) {}

    //////////////////////////////////////////////////////////////////////////
    void cleanupObjects();
    bool makeNewCall(const AorContact& ac, const resip::Data& sdpfile);
    void addCall(SSDialogSet* call);
    void eraseCall(SSDialogSet* call);

private:
    std::unique_ptr<CmdRunner>  mConfig;
    bool mRunning;
    resip::FdPollGrp            *mFdPollGrp;
    resip::AsyncProcessHandler  *mAsyncProcessHandler;
    resip::SipStack             *mSipStack;
    resip::ThreadIf             *mStackThread;
    resip::DialogUsageManager   *mDum;
    resip::ThreadIf             *mDumThread;
    std::shared_ptr<resip::MasterProfile>   mMasterProfile;
    resip::RegistrationPersistenceManager*  mRegMgr;
    std::shared_ptr<resip::Profile> mProxyUdp;
    std::shared_ptr<resip::Profile> mProxyTcp;
    HashMap<UInt64, AorContact>     mRegs;
    HashMap<UInt64, SSDialogSet*>   mCalls;
    HashMap<resip::Uri, UInt64>     mAor2Id;
    static UInt64 sRID;
    static UInt64 sCID;
};

class SSDialogSetFactory : public resip::AppDialogSetFactory
{
public:
    SSDialogSetFactory(SimpleSBC& ss);
    resip::AppDialogSet* createAppDialogSet(resip::DialogUsageManager& ss, const resip::SipMessage&);
private:
    SimpleSBC& mSbc;
};

class SSDialogSet : public resip::AppDialogSet
{
public:
    SSDialogSet(SimpleSBC& ss);
    ~SSDialogSet();

    void initiateCall(const resip::NameAddr& target, std::shared_ptr<resip::UserProfile> profile, const resip::Data& sdpfile);
    bool reinvite(const resip::Data& sdpfile);
    void terminateCall();

    virtual void onNewSession(resip::ClientInviteSessionHandle h, resip::InviteSession::OfferAnswerType oat, const resip::SipMessage& msg);
    virtual void onNewSession(resip::ServerInviteSessionHandle h, resip::InviteSession::OfferAnswerType oat, const resip::SipMessage& msg) {}
    virtual void onFailure(resip::ClientInviteSessionHandle h, const resip::SipMessage& msg);
    virtual void onEarlyMedia(resip::ClientInviteSessionHandle, const resip::SipMessage&, const resip::SdpContents&) {}
    virtual void onProvisional(resip::ClientInviteSessionHandle, const resip::SipMessage& msg);
    virtual void onConnected(resip::ClientInviteSessionHandle h, const resip::SipMessage& msg);
    virtual void onConnected(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onStaleCallTimeout(resip::ClientInviteSessionHandle) {}
    virtual void onTerminated(resip::InviteSessionHandle h, resip::InviteSessionHandler::TerminatedReason reason, const resip::SipMessage* msg) {}
    virtual void onRedirected(resip::ClientInviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onAnswer(resip::InviteSessionHandle, const resip::SipMessage& msg, const resip::SdpContents&);
    virtual void onOffer(resip::InviteSessionHandle handle, const resip::SipMessage& msg, const resip::SdpContents& offer) {}
    virtual void onRemoteSdpChanged(resip::InviteSessionHandle, const resip::SipMessage& msg, const resip::SdpContents& sdp);
    virtual void onOfferRequestRejected(resip::InviteSessionHandle, const resip::SipMessage& msg);
    virtual void onOfferRequired(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onOfferRejected(resip::InviteSessionHandle, const resip::SipMessage* msg) {}
    virtual void onInfo(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onInfoSuccess(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onInfoFailure(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onRefer(resip::InviteSessionHandle, resip::ServerSubscriptionHandle, const resip::SipMessage& msg) {}
    virtual void onReferAccepted(resip::InviteSessionHandle, resip::ClientSubscriptionHandle, const resip::SipMessage& msg) {}
    virtual void onReferRejected(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onReferNoSub(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onMessage(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onMessageSuccess(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onMessageFailure(resip::InviteSessionHandle, const resip::SipMessage& msg) {}
    virtual void onForkDestroyed(resip::ClientInviteSessionHandle) {}
    virtual void onReadyToSend(resip::InviteSessionHandle, resip::SipMessage& msg) {}
    virtual void onFlowTerminated(resip::InviteSessionHandle) {}

    virtual void onTrying(resip::AppDialogSetHandle, const resip::SipMessage& msg);
    virtual void onNonDialogCreatingProvisional(resip::AppDialogSetHandle, const resip::SipMessage& msg) {}

    EncodeStream& dump(EncodeStream& strm) const;

protected:
    void makeOffer(resip::SdpContents& offer, const resip::Data& sdpfile);
    bool readSdpFromFile(resip::SdpContents& sdp, const resip::Data& sdpfile);
private:
    SimpleSBC& mSbc;
    resip::InviteSessionHandle mInviteSessionHandle;
};


#endif // #if !defined(SIPSVR__H)
