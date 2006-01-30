// **********************************************************************
//
// Copyright (c) 2003-2005 ZeroC, Inc. All rights reserved.
//
// This copy of Ice-E is licensed to you under the terms described in the
// ICEE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceE/IceE.h>
#include <TestCommon.h>
#include <TestApplication.h>
#include <deque>
#include <list>
#include <MyByteSeq.h>
#include <TestI.h>

using namespace std;

class CustomTestApplication : public TestApplication
{
public:

    CustomTestApplication() :
        TestApplication("custom collocated")
    {
    }

    virtual int
    run(int argc, char* argv[])
    {
	Ice::PropertiesPtr properties = Ice::createProperties();

	properties->setProperty("TestAdapter.Endpoints", "default -p 12345 -t 10000");
	//properties->setProperty("Ice.Trace.Network", "5");
	//properties->setProperty("Ice.Trace.Protocol", "5");

	loadConfig(properties);
	setCommunicator(Ice::initializeWithProperties(argc, argv, properties));

        Ice::ObjectAdapterPtr adapter = communicator()->createObjectAdapter("TestAdapter");
        adapter->add(new TestIntfI(communicator()), Ice::stringToIdentity("test"));
        adapter->activate();

        Test::TestIntfPrx allTests(const Ice::CommunicatorPtr&);
        allTests(communicator());

        return EXIT_SUCCESS;
    }
};

#ifdef _WIN32_WCE

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
    CustomTestApplication app;
    return app.main(hInstance);
}

#else

int
main(int argc, char** argv)
{
    CustomTestApplication app;
    return app.main(argc, argv);
}

#endif
