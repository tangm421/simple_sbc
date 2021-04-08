
#include "simple_sbc.h"
#include "cmd_option.h"
#include "rutil/ThreadIf.hxx"
using namespace resip;

#include <iostream>
using namespace std;

class CommandInterface : public resip::ThreadIf
{
public:
    CommandInterface(SimpleSBC* sbc) : mSbc(sbc){}
    void thread()
    {
        cout << "Type 'help' for command description" << endl;
        while (!isShutdown())
        {
            string line;
            cout << "# ";
            getline(cin, line);
            if (line.empty())
            {
                continue;
            }

            unique_ptr<Cmd> cmd = CmdFactory::instanceCmd(line, mSbc);
            if (!cmd)
            {
                continue;
            }

            if (cmd->getCommandType() == Cmd::Exit)
            {
                shutdown();
                break;
            }

            if (!cmd->run())
            {
                cerr << cmd->getLastErr() << endl;
            }
        }

        mSbc->onSignal(2);
    }
private:
    SimpleSBC* mSbc;
};

int main(int argc, char* argv[])
{
    std::unique_ptr<CmdRunner> runnerCmd = CmdFactory::instanceRunnerCmd(argc, argv, "SimpleSBC 0.0.1");
    if (!runnerCmd->run())
    {
        cerr << runnerCmd->getLastErr() << endl;
        return 0;
    }

    initNetwork();

    SimpleSBC sbc;
    if (!sbc.startup(std::move(runnerCmd)))
    {
        cerr << "Failed to start SimpleSBC, exiting..." << endl;
        exit(-1);
    }

    CommandInterface intf(&sbc);
    intf.run();

    sbc.mainLoop();

    sbc.shutdown();

    return 0;
}
