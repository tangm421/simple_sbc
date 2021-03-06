
#include "simple_sbc.h"
#include "ss_subsystem.h"

#include "rutil/Data.hxx"
#include "resip/stack/SipStack.hxx"
#include "resip/stack/EventStackThread.hxx"
#include "resip/stack/InteropHelper.hxx"
#include "resip/stack/MessageFilterRule.hxx"
#include "resip/dum/ClientInviteSession.hxx"
//#include "resip/dum/InMemoryRegistrationDatabase.hxx"
#include "resip/dum/InMemorySyncRegDb.hxx"
#include "resip/dum/ServerRegistration.hxx"
#include "resip/dum/DialogUsageManager.hxx"
#include "resip/dum/MasterProfile.hxx"
#include "resip/dum/DumThread.hxx"
#include "resip/dum/ClientAuthManager.hxx"
#include "resip/dum/ServerAuthManager.hxx"
#include "resip/dum/KeepAliveManager.hxx"
#include "rutil/Logger.hxx"
#include "rutil/DnsUtil.hxx"
#include "rutil/ResipAssert.h"
using namespace resip;

#include <iostream>
using namespace std;


#define RESIPROCATE_SUBSYSTEM SipSvrSubsystem::SSMODULE


UInt64 SimpleSBC::sRID = 1;
UInt64 SimpleSBC::sCID = 1;

SimpleSBC::SimpleSBC()
    : mRunning(false)
    , mFdPollGrp(0)
    , mAsyncProcessHandler(0)
    , mSipStack(0)
    , mStackThread(0)
    , mDum(0)
    , mDumThread(0)
    , mMasterProfile(new MasterProfile)
    , mRegMgr(0)
{
}

SimpleSBC::~SimpleSBC()
{
    if (mRunning) shutdown();
}

bool SimpleSBC::startup(std::unique_ptr<CmdRunner> cmd)
{
    if (mRunning) return true;
    installSignalHandler();

    mConfig = std::move(cmd);

    // Initialize resip logger
    Log::initialize(mConfig->mLogType, mConfig->mLogLevel, mConfig->getCommandName(), mConfig->mLogFile.c_str());
    Log::setMaxByteCount(mConfig->mLogFileSize);// 5242880 /*5 Mb */
    Log::setKeepAllLogFiles(mConfig->mKeepAllLogFiles ? true : false);

    InfoLog(<< "Starting SimpleSBC...");
    cout << "Starting SimpleSBC..." << endl;

    if (!createSipStack())
    {
        return false;
    }

    if (!createRegistrationManager())
    {
        return false;
    }

    if (!createDialogUsageManager())
    {
        return false;
    }

    mSipStack->run();
    if (mStackThread)
    {
        mStackThread->run();
    }
    if (mDumThread)
    {
        mDumThread->run();
    }

    mRunning = true;
    return true;
}

void SimpleSBC::shutdown()
{
    if (mRunning) return;

    if (mDumThread)
    {
        mDumThread->shutdown();
    }
    if (mStackThread)
    {
        mStackThread->shutdown();
    }
    if (mDumThread)
    {
        mDumThread->join();
    }
    if (mStackThread)
    {
        mStackThread->join();
    }

    cleanupObjects();
    mRunning = false;
}

bool SimpleSBC::makeNewCall(const resip::Uri& aor, const Data& sdpfile)
{
    auto id = mAor2Id.find(aor);
    if (id == mAor2Id.end())
    {
        cerr << aor << " not exist in register manager anymore!, Type `show reg` for details" << endl;
        return false;
    }

    return makeNewCall(id->second, sdpfile);
}

bool SimpleSBC::makeNewCall(UInt64 id, const resip::Data& sdpfile)
{
    auto reg = mRegs.find(id);
    if (reg == mRegs.end())
    {
        cerr << id << " not exist in register manager anymore!, Type `show reg` for details" << endl;
        return false;
    }

    return makeNewCall(reg->second, sdpfile);
}

void SimpleSBC::finishCall(const std::list<UInt64>& cids)
{
    for (auto id : cids)
    {
        auto ret = mCalls.find(id);
        if (ret != mCalls.end())
        {
            ret->second->terminateCall();
        }
    }
}

bool SimpleSBC::makeReinvite(UInt64 id, const resip::Data& sdpfile)
{
    auto ret = mCalls.find(id);
    if (ret == mCalls.end())
    {
        cerr << id << " not exist in call manager anymore!, Type `show call` for details" << endl;
        return false;
    }

    return ret->second->reinvite(sdpfile);
}

void SimpleSBC::showAllReg()
{
    for (auto i : mRegs)
    {
        const ContactList* cl = i.second.mContacts;
        cout << i.first << " --> Aor:" << i.second.mAor << endl;
        for (auto j : *cl)
        {
            UInt64 expire = 0;
            if (j.mRegExpires > ResipClock::getTimeSecs())
            {
                expire = j.mRegExpires - ResipClock::getTimeSecs();
            }
            cout << "      --Contact:" << j.mContact << endl
                 << "      --Expires In:" << expire << endl;
        }
    }
}

void SimpleSBC::showAllCall()
{
    for (auto i : mCalls)
    {
        cout << i.first << " --> " << *i.second << endl;
    }
}

bool SimpleSBC::createSipStack()
{
    resip_assert(!mFdPollGrp);
    resip_assert(!mAsyncProcessHandler);
    resip_assert(!mSipStack);
    resip_assert(!mStackThread);

    // Create EventThreadInterruptor used to wake up the stack for reasons other than an Fd signalling
    mFdPollGrp = FdPollGrp::create();
    mAsyncProcessHandler = new EventThreadInterruptor(*mFdPollGrp);
    mSipStack = new SipStack(0,
                             DnsStub::EmptyNameserverList,
                             mAsyncProcessHandler,
                             false,
                             0,
                             0,
                             mFdPollGrp,
                             false
                             );
    mStackThread = new EventStackThread(*mSipStack,
        *dynamic_cast<EventThreadInterruptor*>(mAsyncProcessHandler),
        *mFdPollGrp);

    mSipStack->statisticsManagerEnabled() = false;

    return addTransports();
}

bool SimpleSBC::createDialogUsageManager()
{
    resip_assert(!mDum);
    resip_assert(!mDumThread);

    mDum = new DialogUsageManager(*mSipStack);

    resip::MessageFilterRuleList ruleList;
    resip::MessageFilterRule::MethodList methodList;
    methodList.push_back(resip::INVITE);
    methodList.push_back(resip::ACK);
    methodList.push_back(resip::CANCEL);
    methodList.push_back(resip::OPTIONS);
    methodList.push_back(resip::BYE);
    methodList.push_back(resip::UPDATE);
    methodList.push_back(resip::REGISTER);
    methodList.push_back(resip::MESSAGE);
    methodList.push_back(resip::INFO);
    ruleList.push_back(MessageFilterRule(resip::MessageFilterRule::SchemeList(),
        resip::MessageFilterRule::DomainIsMe,
        methodList));
    mDum->setMessageFilterRuleList(ruleList);

    unique_ptr<AppDialogSetFactory> dsf(new SSDialogSetFactory(*this));
    mDum->setAppDialogSetFactory(std::move(dsf));

    mDum->setServerRegistrationHandler(this);
    mDum->setInviteSessionHandler(this);
    mDum->setDialogSetHandler(this);
    mDum->setRegistrationPersistenceManager(mRegMgr);
    mDum->setKeepAliveManager(std::unique_ptr<KeepAliveManager>(new KeepAliveManager));

    // MasterProfile settings
    mMasterProfile->addSupportedMethod(REGISTER);
    mMasterProfile->addSupportedMethod(MESSAGE);
    mMasterProfile->addSupportedMethod(INFO);
    // basic Profile settings
    mMasterProfile->setRportEnabled(InteropHelper::getRportEnabled());
    mMasterProfile->setOutboundDecorator(std::make_shared<SdpMessageDecorator>());

    mDum->setMasterProfile(mMasterProfile);

    addDomains(*mDum);

    // custom Profile settings
    mProxyUdp = std::make_shared<Profile>(mMasterProfile);
    mProxyUdp->setKeepAliveTimeForDatagram(30);
    mProxyUdp->setUserAgent("SimpleSBC/UDP");

    mProxyTcp = std::make_shared<Profile>(mMasterProfile);
    mProxyTcp->setKeepAliveTimeForStream(120);
    mProxyTcp->setUserAgent("SimpleSBC/TCP");

    mDumThread = new DumThread(*mDum);

    dynamic_cast<InMemorySyncRegDb*>(mRegMgr)->addHandler(this);

    return true;
}

bool SimpleSBC::createRegistrationManager()
{
    resip_assert(!mRegMgr);
    mRegMgr = new InMemorySyncRegDb();
    return true;
}

void SimpleSBC::addDomains(resip::TransactionUser& tu)
{
    std::list<std::pair<Data, Data> > interfaces = DnsUtil::getInterfaces();
    for (auto i : interfaces)
    {
        if (DnsUtil::isIpV4Address(i.second)) tu.addDomain(i.second);
    }
}

bool SimpleSBC::addTransports()
{
    resip_assert(mSipStack);
    try
    {
        if (mConfig->mSipUdpPort)
        {
            mSipStack->addTransport(UDP, mConfig->mSipUdpPort, V4, StunDisabled, mConfig->mSipAddress);
        }
        if (mConfig->mSipTcpPort)
        {
            mSipStack->addTransport(TCP, mConfig->mSipTcpPort, V4, StunDisabled, mConfig->mSipAddress);
        }
    }
    catch (BaseException& e)
    {
        std::cerr << "Likely a port is already in use" << endl;
        InfoLog(<< "Caught: " << e);
        return false;
    }
    return true;
}

void SimpleSBC::onAorModified(const resip::Uri& aor, const ContactList& contacts)
{
    if (contacts.empty())
    {
        auto id = mAor2Id.find(aor);
        if (id != mAor2Id.end())
        {
            mRegs.erase(id->second);
            mAor2Id.erase(id);
        }
    }
    else
    {
        auto id = mAor2Id.find(aor);
        if (id != mAor2Id.end())
        {
            mRegs.insert(std::make_pair(id->second, AorContact(aor, &contacts)));
        }
        else
        {
            mAor2Id[aor] = sRID;
            mRegs.insert(std::make_pair(sRID, AorContact(aor, &contacts)));
            ++sRID;
        }
    }
}

void SimpleSBC::onRefresh(ServerRegistrationHandle h, const SipMessage& reg)
{
    h->accept();
}

void SimpleSBC::onRemove(ServerRegistrationHandle h, const SipMessage& reg)
{
    h->accept();
}

void SimpleSBC::onRemoveAll(ServerRegistrationHandle h, const SipMessage& reg)
{
    h->accept();
}

void SimpleSBC::onAdd(ServerRegistrationHandle h, const SipMessage& reg)
{
    h->accept();
}

void SimpleSBC::onQuery(ServerRegistrationHandle h, const SipMessage& reg)
{
    h->accept();
}


void SimpleSBC::onNewSession(ClientInviteSessionHandle h, InviteSession::OfferAnswerType oat, const SipMessage& msg)
{
    dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get())->onNewSession(h, oat, msg);
}

void SimpleSBC::onProvisional(ClientInviteSessionHandle h, const SipMessage& msg)
{
    dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get())->onProvisional(h, msg);
}

void SimpleSBC::onConnected(ClientInviteSessionHandle h, const SipMessage& msg)
{
    dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get())->onConnected(h, msg);
}

void SimpleSBC::onTerminated(InviteSessionHandle h, InviteSessionHandler::TerminatedReason reason, const SipMessage* related /*= 0*/)
{
    Data reasonData;

    switch (reason)
    {
    case InviteSessionHandler::RemoteBye:
        reasonData = "received a BYE from peer";
        break;
    case InviteSessionHandler::RemoteCancel:
        reasonData = "received a CANCEL from peer";
        break;
    case InviteSessionHandler::Rejected:
        reasonData = "received a rejection from peer";
        break;
    case InviteSessionHandler::LocalBye:
        reasonData = "ended locally via BYE";
        break;
    case InviteSessionHandler::LocalCancel:
        reasonData = "ended locally via CANCEL";
        break;
    case InviteSessionHandler::Replaced:
        reasonData = "ended due to being replaced";
        break;
    case InviteSessionHandler::Referred:
        reasonData = "ended due to being referred";
        break;
    case InviteSessionHandler::Error:
        reasonData = "ended due to an error";
        break;
    case InviteSessionHandler::Timeout:
        reasonData = "ended due to a timeout";
        break;
    default:
        assert(false);
        break;
    }

    if (related)
    {
        InfoLog(<< "onTerminated: reason=" << reasonData << ", msg=" << related->brief());
    }
    else
    {
        InfoLog(<< "onTerminated: reason=" << reasonData);
    }

    SSDialogSet* ds = dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get());
    if (ds)
    {
        eraseCall(ds);
    }
}

void SimpleSBC::onAnswer(InviteSessionHandle h, const SipMessage& msg, const SdpContents& sdp)
{
    dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get())->onAnswer(h, msg, sdp);
}

void SimpleSBC::onRemoteSdpChanged(InviteSessionHandle h, const SipMessage& msg, const SdpContents& sdp)
{
    dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get())->onRemoteSdpChanged(h, msg, sdp);
}

void SimpleSBC::onOfferRequestRejected(InviteSessionHandle h, const SipMessage& msg)
{
    dynamic_cast<SSDialogSet*>(h->getAppDialogSet().get())->onOfferRequestRejected(h, msg);
}

void SimpleSBC::onTrying(resip::AppDialogSetHandle h, const resip::SipMessage& msg)
{
    dynamic_cast<SSDialogSet*>(h.get())->onTrying(h, msg);
}

void SimpleSBC::cleanupObjects()
{
    delete mRegMgr; mRegMgr = 0;
    delete mDumThread; mDumThread = 0;
    delete mDum; mDum = 0;
    delete mStackThread; mStackThread = 0;
    delete mSipStack; mSipStack = 0;
    delete mAsyncProcessHandler; mAsyncProcessHandler = 0;
    delete mFdPollGrp; mFdPollGrp = 0;
}


bool SimpleSBC::makeNewCall(const AorContact& ac, const resip::Data& sdpfile)
{
    const ContactList* cl = ac.mContacts;
    if (cl->empty())
    {
        cerr << ac.mAor << " has no contact" << endl;
        return false;
    }

    UInt64 now = Timer::getTimeSecs();
    for (auto rec : *cl)
    {
        if (rec.mRegExpires > now)
        {
            std::shared_ptr<UserProfile> userProfile;
            if (rec.mReceivedFrom.getType() == resip::UDP)
            {
                userProfile = std::make_shared<UserProfile>(mProxyUdp);
            }
            else if (rec.mReceivedFrom.getType() == resip::TCP)
            {
                userProfile = std::make_shared<UserProfile>(mProxyUdp);
            }
            else
            {
                ErrLog(<< "Only support UDP and TCP for INVITE!");
                resip_assert(0);
            }

            userProfile->setDefaultFrom(userProfile->getAnonymousUserProfile()->getDefaultFrom());
            userProfile->clientOutboundEnabled() = true;
            userProfile->setClientOutboundFlowTuple(rec.mReceivedFrom);

            SSDialogSet* newCall = new SSDialogSet(*this);
            newCall->initiateCall(rec.mContact, std::move(userProfile), sdpfile);

            addCall(newCall);

            return true;
        }
    }

    cerr << ac.mAor << " has no valid contact!" << endl;

    return false;
}

void SimpleSBC::addCall(SSDialogSet* call)
{
    mCalls[sCID++] = call;
}

void SimpleSBC::eraseCall(SSDialogSet* call)
{
    for (auto i : mCalls)
    {
        if (i.second == call)
        {
            mCalls.erase(i.first);
            break;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
SSDialogSetFactory::SSDialogSetFactory(SimpleSBC& ss) : mSbc(ss)
{
}

resip::AppDialogSet* SSDialogSetFactory::createAppDialogSet(DialogUsageManager& dum, const SipMessage& msg)
{
    switch (msg.method())
    {
    case INVITE:
        return new SSDialogSet(mSbc);
        break;
    default:
        return AppDialogSetFactory::createAppDialogSet(dum, msg);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
SSDialogSet::SSDialogSet(SimpleSBC& ss) : AppDialogSet(*ss.mDum), mSbc(ss)
{
}

SSDialogSet::~SSDialogSet()
{
    cerr << *this << endl;
}

void SSDialogSet::initiateCall(const resip::NameAddr& target, std::shared_ptr<resip::UserProfile> profile, const resip::Data& sdpfile)
{
    SdpContents offer;
    makeOffer(offer, sdpfile);
    auto invite = mSbc.getDialogUsageManager().makeInviteSession(target, std::move(profile), &offer, this);
    mSbc.getDialogUsageManager().send(std::move(invite));
}

bool SSDialogSet::reinvite(const resip::Data& sdpfile)
{
    if (!mInviteSessionHandle->isConnected())
    {
        cerr << "call not connected, Illegal operation" << endl;
        return false;
    }

    if (sdpfile.empty())
    {
        const SdpContents& sdp = mInviteSessionHandle->getLocalSdp();
        mInviteSessionHandle->provideOffer(sdp);
    }
    else
    {
        SdpContents sdp;
        if (!readSdpFromFile(sdp, sdpfile))
        {
            return false;
        }
        else
        {
            mInviteSessionHandle->provideOffer(sdp);
        }
    }

    return true;
}

void SSDialogSet::terminateCall()
{
    if (mInviteSessionHandle.isValid())
    {
        mInviteSessionHandle->end(InviteSession::UserHangup);
    }
    else
    {
        AppDialogSet::end();
    }
}

void SSDialogSet::onNewSession(resip::ClientInviteSessionHandle h, resip::InviteSession::OfferAnswerType oat, const resip::SipMessage& msg)
{
    mInviteSessionHandle = h->getSessionHandle();
}

void SSDialogSet::onFailure(resip::ClientInviteSessionHandle h, const resip::SipMessage& msg)
{
    mInviteSessionHandle = h->getSessionHandle();
    InfoLog(<< "Invite failure...");
}

void SSDialogSet::onProvisional(resip::ClientInviteSessionHandle h, const resip::SipMessage& msg)
{
    mInviteSessionHandle = h->getSessionHandle();
    InfoLog(<< "Received 180 Ringing...");
}

void SSDialogSet::onConnected(resip::ClientInviteSessionHandle h, const resip::SipMessage& msg)
{
    mInviteSessionHandle = h->getSessionHandle();
    InfoLog(<< "Invite Session Connected.");
}

void SSDialogSet::onTrying(resip::AppDialogSetHandle h, const resip::SipMessage& msg)
{
    InfoLog(<< "Received 100 Trying...");
}

void SSDialogSet::onAnswer(resip::InviteSessionHandle h, const resip::SipMessage& msg, const resip::SdpContents& sdp)
{
    mInviteSessionHandle = h->getSessionHandle();
    InfoLog(<< "Received answer..." << msg);
    const NameAddr& from = msg.header(h_From);
    const NameAddr& contact = msg.header(h_Contacts).front();
    InfoLog(<< "from displayname:" << from.displayName() << ", contact displayname:" << contact.displayName());
}


void SSDialogSet::onRemoteSdpChanged(resip::InviteSessionHandle h, const resip::SipMessage& msg, const resip::SdpContents& sdp)
{
    mInviteSessionHandle = h->getSessionHandle();
    InfoLog(<< "Received onRemoteSdpChanged..." << msg);
}

void SSDialogSet::onOfferRequestRejected(resip::InviteSessionHandle h, const resip::SipMessage& msg)
{
    mInviteSessionHandle = h->getSessionHandle();
    InfoLog(<< "Received onOfferRequestRejected..." << msg);
}

EncodeStream& SSDialogSet::dump(EncodeStream& strm) const
{
    if (mInviteSessionHandle.isValid())
    {
        return strm << mInviteSessionHandle->peerAddr();
    }
    else
    {
        return AppDialogSet::dump(strm);
    }
}

void SSDialogSet::makeOffer(resip::SdpContents& offer, const resip::Data& sdpfile)
{
    static Data txt("v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=basicClient\r\n"
        "c=IN IP4 10.18.0.200\r\n"
        "t=0 0\r\n"
        "m=audio 45678 RTP/AVP 0 101\r\n"
        "a=rtpmap:0 pcmu/8000\r\n"
        "a=rtpmap:101 telephone-event/8000\r\n"
        "a=fmtp:101 0-15\r\n");

    HeaderFieldValue hfv(txt.data(), txt.size());
    Mime type("application", "sdp");
    SdpContents offerSdp(hfv, type);

    // Set sessionid and version for this offer
    UInt64 currentTime = Timer::getTimeMicroSec();
    offerSdp.session().origin().getSessionId() = currentTime;
    offerSdp.session().origin().getVersion() = currentTime;

    offer = offerSdp;

    if (sdpfile.empty())
    {
        readSdpFromFile(offer, sdpfile);
    }
}

bool SSDialogSet::readSdpFromFile(resip::SdpContents& sdp, const resip::Data& sdpfile)
{
    try
    {
        Data txt(Data::fromFile(sdpfile));
        HeaderFieldValue hfv(txt.data(), txt.size());
        Mime type("application", "sdp");
        SdpContents offerSdp(hfv, type);

        sdp = offerSdp;

        return true;
    }
    catch (...)
    {
        cerr << "failed to read sdp from file:" << sdpfile << ", use default sdp" << endl;
        return false;
    }
}

