#if !defined(SIPSVR_SUBSYSTEM__H)
#define SIPSVR_SUBSYSTEM__H

#include <rutil/Subsystem.hxx>


class SipSvrSubsystem : public resip::Subsystem
{
public:
    // Add new systems below
    static SipSvrSubsystem SSMODULE;

private:
    explicit SipSvrSubsystem(const char* rhs) : resip::Subsystem(rhs) {};
    explicit SipSvrSubsystem(const resip::Data& rhs);
};

#endif // #if !defined(SIPSVR_SUBSYSTEM__H)
