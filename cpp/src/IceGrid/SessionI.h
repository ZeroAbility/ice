// **********************************************************************
//
// Copyright (c) 2003-2006 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef ICEGRID_SESSIONI_H
#define ICEGRID_SESSIONI_H

#include <IceUtil/Mutex.h>
#include <IceGrid/Session.h>
#include <IceGrid/ReapThread.h>

namespace IceGrid
{

class Database;
typedef IceUtil::Handle<Database> DatabasePtr;

class TraceLevels;
typedef IceUtil::Handle<TraceLevels> TraceLevelsPtr;

class AllocationRequest;
typedef IceUtil::Handle<AllocationRequest> AllocationRequestPtr;

class Allocatable;
typedef IceUtil::Handle<Allocatable> AllocatablePtr;

class WaitQueue;
typedef IceUtil::Handle<WaitQueue> WaitQueuePtr;

class SessionI;
typedef IceUtil::Handle<SessionI> SessionIPtr;

class SessionReapable : public Reapable
{
public:

    SessionReapable(const Ice::ObjectAdapterPtr&, const SessionIPtr&, const SessionPrx&);
    virtual ~SessionReapable();

    virtual IceUtil::Time timestamp() const;
    virtual void destroy(bool);

private:

    const Ice::ObjectAdapterPtr _adapter;
    const SessionIPtr _session;
    const SessionPrx _proxy;
};

class SessionI : virtual public Session, public IceUtil::Mutex
{
public:

    virtual ~SessionI();

    virtual void keepAlive(const Ice::Current&);
    virtual int getTimeout(const Ice::Current&) const;
    virtual void allocateObjectById_async(const AMD_Session_allocateObjectByIdPtr&, const Ice::Identity&,
					  const Ice::Current&);
    virtual void allocateObjectByType_async(const AMD_Session_allocateObjectByTypePtr&, const std::string&,
					    const Ice::Current&);
    virtual void releaseObject(const Ice::Identity&, const Ice::Current&);
    virtual void setAllocationTimeout(int, const Ice::Current&);
    virtual void destroy(const Ice::Current&);

    IceUtil::Time timestamp() const;
    int getAllocationTimeout() const;
    const WaitQueuePtr& getWaitQueue() const { return _waitQueue; }
    const std::string& getUserId() const { return _userId; }

    bool addAllocationRequest(const AllocationRequestPtr&);
    void removeAllocationRequest(const AllocationRequestPtr&);
    bool addAllocation(const AllocatablePtr&);
    void removeAllocation(const AllocatablePtr&);

protected:

    SessionI(const std::string&, const std::string&, const DatabasePtr&, const Ice::ObjectAdapterPtr&, 
	     const WaitQueuePtr&, int);

    const std::string _userId;
    const std::string _prefix;
    const int _timeout;
    const TraceLevelsPtr _traceLevels;
    const DatabasePtr _database;
    const WaitQueuePtr _waitQueue;
    bool _destroyed;
    IceUtil::Time _timestamp;
    int _allocationTimeout;
    std::set<AllocationRequestPtr> _requests;
    std::set<AllocatablePtr> _allocations;
};

class ClientSessionI : public SessionI
{
public:

    ClientSessionI(const std::string&, const DatabasePtr&, const Ice::ObjectAdapterPtr&, const WaitQueuePtr&, int);
};

class ClientSessionManagerI : virtual public SessionManager
{
public:

    ClientSessionManagerI(const  DatabasePtr&, const ReapThreadPtr&, const WaitQueuePtr&, int);
    
    virtual Glacier2::SessionPrx create(const std::string&, const Glacier2::SessionControlPrx&, const Ice::Current&);
    virtual SessionPrx createLocalSession(const std::string&, const Ice::Current&);

private:

    const DatabasePtr _database;
    const ReapThreadPtr _reaper;
    const WaitQueuePtr _waitQueue;
    int _timeout;
};

};

#endif
