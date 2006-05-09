// **********************************************************************
//
// Copyright (c) 2003-2006 ZeroC, Inc. All rights reserved.
//
// This copy of Ice-E is licensed to you under the terms described in the
// ICEE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceE/ObjectAdapter.h>
#include <IceE/ObjectAdapterFactory.h>
#include <IceE/UUID.h>
#include <IceE/Instance.h>
#include <IceE/ProxyFactory.h>
#include <IceE/ReferenceFactory.h>
#include <IceE/EndpointFactory.h>
#include <IceE/IncomingConnectionFactory.h>
#include <IceE/OutgoingConnectionFactory.h>
#include <IceE/ServantManager.h>
#include <IceE/LocalException.h>
#include <IceE/Properties.h>
#include <IceE/Functional.h>
#ifdef ICEE_HAS_LOCATOR
#    include <IceE/LocatorInfo.h>
#    include <IceE/Locator.h>
#endif
#ifdef ICEE_HAS_ROUTER
#    include <IceE/RouterInfo.h>
#    include <IceE/Router.h>
#endif
#include <IceE/Endpoint.h>
#include <IceE/LoggerUtil.h>
#include <ctype.h>

using namespace std;
using namespace Ice;
using namespace IceInternal;

bool
Ice::operator==(const ::Ice::ObjectAdapter& l, const ::Ice::ObjectAdapter& r)
{
    return &l == &r;
}

bool
Ice::operator!=(const ::Ice::ObjectAdapter& l, const ::Ice::ObjectAdapter& r)
{
    return &l != &r;
}

bool
Ice::operator<(const ::Ice::ObjectAdapter& l, const ::Ice::ObjectAdapter& r)
{
    return &l < &r;
}

void
IceInternal::incRef(::Ice::ObjectAdapter* p)
{
    p->__incRef();
}

void
IceInternal::decRef(::Ice::ObjectAdapter* p)
{
    p->__decRef();
}

string
Ice::ObjectAdapter::getName() const
{
    //
    // No mutex lock necessary, _name is immutable.
    //
    return _name;
}

CommunicatorPtr
Ice::ObjectAdapter::getCommunicator() const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();

    return _communicator;
}

void
Ice::ObjectAdapter::activate()
{
#ifdef ICEE_HAS_LOCATOR
    LocatorInfoPtr locatorInfo;
#endif
    bool printAdapterReady = false;

    {    
	IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
	
	checkForDeactivation();

	if(!_printAdapterReadyDone)
	{
#ifdef ICEE_HAS_LOCATOR
	    locatorInfo = _locatorInfo;
#endif
	    printAdapterReady = 
	        _instance->initializationData().properties->getPropertyAsInt("Ice.PrintAdapterReady") > 0;
	    _printAdapterReadyDone = true;
	}
	
	for_each(_incomingConnectionFactories.begin(), _incomingConnectionFactories.end(),
		 Ice::voidMemFun(&IncomingConnectionFactory::activate));	
    }

#ifdef ICEE_HAS_LOCATOR
    if(!_id.empty())
    {
	//
	// We must call on the locator registry outside the thread
	// synchronization, to avoid deadlocks.
	//
	LocatorRegistryPrx locatorRegistry;
	if(locatorInfo)
	{
	    //
	    // TODO: This might throw if we can't connect to the
	    // locator. Shall we raise a special exception for the
	    // activate operation instead of a non obvious network
	    // exception?
	    //
	    locatorRegistry = locatorInfo->getLocatorRegistry();
	}

	if(locatorRegistry)
	{
	    try
	    {
		Identity ident;
		ident.name = "dummy";
		if(_replicaGroupId.empty())
		{
		    locatorRegistry->setAdapterDirectProxy(_id, createDirectProxy(ident));
		}
		else
		{
		    locatorRegistry->setReplicatedAdapterDirectProxy(_id, _replicaGroupId, createDirectProxy(ident));
		}
	    }
	    catch(const ObjectAdapterDeactivatedException&)
	    {
		// IGNORE: The object adapter is already inactive.
	    }
	    catch(const AdapterNotFoundException&)
	    {
		NotRegisteredException ex(__FILE__, __LINE__);
		ex.kindOfObject = "object adapter";
		ex.id = _id;
		throw ex;
	    }
	    catch(const InvalidReplicaGroupIdException&)
	    {
		NotRegisteredException ex(__FILE__, __LINE__);
		ex.kindOfObject = "replica group";
		ex.id = _replicaGroupId;
		throw ex;
	    }
	    catch(const AdapterAlreadyActiveException&)
	    {
		ObjectAdapterIdInUseException ex(__FILE__, __LINE__);
		ex.id = _id;
		throw ex;
	    }
	}
    }
#endif

    if(printAdapterReady)
    {
	printf("%s ready\n", _name.c_str());
	fflush(stdout);
    }
}

void
Ice::ObjectAdapter::hold()
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
	
    for_each(_incomingConnectionFactories.begin(), _incomingConnectionFactories.end(),
	     Ice::voidMemFun(&IncomingConnectionFactory::hold));
}
    
void
Ice::ObjectAdapter::waitForHold()
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();

    for_each(_incomingConnectionFactories.begin(), _incomingConnectionFactories.end(),
	     Ice::constVoidMemFun(&IncomingConnectionFactory::waitUntilHolding));
}

void
Ice::ObjectAdapter::deactivate()
{
    vector<IncomingConnectionFactoryPtr> incomingConnectionFactories;
    OutgoingConnectionFactoryPtr outgoingConnectionFactory;

    {
	IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
	
	//
	// Ignore deactivation requests if the object adapter has already
	// been deactivated.
	//
	if(_deactivated)
	{
	    return;
	}

#ifdef ICEE_HAS_ROUTER
        if(_routerInfo)
        {
            //
            // Remove entry from the router manager.
            //
            _instance->routerManager()->erase(_routerInfo->getRouter());

            //
            //  Clear this object adapter with the router.
            //
            _routerInfo->setAdapter(0);

            //
            // Update all existing outgoing connections.
            //
            _instance->outgoingConnectionFactory()->setRouterInfo(_routerInfo);
        }
#endif

        incomingConnectionFactories = _incomingConnectionFactories;
	outgoingConnectionFactory = _instance->outgoingConnectionFactory();
    
	_deactivated = true;
	
	notifyAll();
    }

    //
    // Must be called outside the thread synchronization, because
    // Connection::destroy() might block when sending a CloseConnection
    // message.
    //
    for_each(incomingConnectionFactories.begin(), incomingConnectionFactories.end(),
	     Ice::voidMemFun(&IncomingConnectionFactory::destroy));
    
    //
    // Must be called outside the thread synchronization, because
    // changing the object adapter might block if there are still
    // requests being dispatched.
    //
    outgoingConnectionFactory->removeAdapter(this);
}

void
Ice::ObjectAdapter::waitForDeactivate()
{
    {
	IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

	//
	// First we wait for deactivation of the adapter itself, and for
	// the return of all direct method calls using this adapter.
	//
	while(!_deactivated || _directCount > 0)
	{
	    wait();
	}

	//
	// If some other thread is currently deactivating, we wait
	// until this thread is finished.
	//
	while(_waitForDeactivate)
	{
	    wait();
	}
	_waitForDeactivate = true;
    }

    //
    // Now we wait until all incoming connection factories are
    // finished.
    //
    for_each(_incomingConnectionFactories.begin(), _incomingConnectionFactories.end(),
	     Ice::voidMemFun(&IncomingConnectionFactory::waitUntilFinished));

    //
    // Now it's also time to clean up our servants and servant
    // locators.
    //
    if(_servantManager)
    {
	_servantManager->destroy();
    }

    ObjectAdapterFactoryPtr objectAdapterFactory;

    {
	IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

	//
	// Signal that waiting is complete.
	//
	_waitForDeactivate = false;
	notifyAll();

	//
	// We're done, now we can throw away all incoming connection
	// factories.
	//
	_incomingConnectionFactories.clear();

	//
	// Remove object references (some of them cyclic).
	//
	_instance = 0;
	_servantManager = 0;
	_communicator = 0;
#ifdef ICEE_HAS_ROUTER
	_routerInfo = 0;
#endif
#ifdef ICEE_HAS_LOCATOR
	_locatorInfo = 0;
#endif
	objectAdapterFactory = _objectAdapterFactory;
	_objectAdapterFactory = 0;
    }

    if(objectAdapterFactory)
    {
        objectAdapterFactory->removeObjectAdapter(_name);
    }
}

void
Ice::ObjectAdapter::destroy()
{
    deactivate();
    waitForDeactivate();
}

ObjectPrx
Ice::ObjectAdapter::add(const ObjectPtr& object, const Identity& ident)
{
    return addFacet(object, ident, "");
}

ObjectPrx
Ice::ObjectAdapter::addFacet(const ObjectPtr& object, const Identity& ident, const string& facet)
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
    checkIdentity(ident);

    _servantManager->addServant(object, ident, facet);

    return newProxy(ident, facet);
}

ObjectPrx
Ice::ObjectAdapter::addWithUUID(const ObjectPtr& object)
{
    return addFacetWithUUID(object, "");
}

ObjectPrx
Ice::ObjectAdapter::addFacetWithUUID(const ObjectPtr& object, const string& facet)
{
    Identity ident;
    ident.name = IceUtil::generateUUID();
    return addFacet(object, ident, facet);
}

ObjectPtr
Ice::ObjectAdapter::remove(const Identity& ident)
{
    return removeFacet(ident, "");
}

ObjectPtr
Ice::ObjectAdapter::removeFacet(const Identity& ident, const string& facet)
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
    checkIdentity(ident);

    return _servantManager->removeServant(ident, facet);
}

FacetMap
Ice::ObjectAdapter::removeAllFacets(const Identity& ident)
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
    checkIdentity(ident);

    return _servantManager->removeAllFacets(ident);
}

ObjectPtr
Ice::ObjectAdapter::find(const Identity& ident) const
{
    return findFacet(ident, "");
}

ObjectPtr
Ice::ObjectAdapter::findFacet(const Identity& ident, const string& facet) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
    checkIdentity(ident);

    return _servantManager->findServant(ident, facet);
}

FacetMap
Ice::ObjectAdapter::findAllFacets(const Identity& ident) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
    checkIdentity(ident);

    return _servantManager->findAllFacets(ident);
}

ObjectPtr
Ice::ObjectAdapter::findByProxy(const ObjectPrx& proxy) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();

    ReferencePtr ref = proxy->__reference();
    return findFacet(ref->getIdentity(), ref->getFacet());
}

ObjectPrx
Ice::ObjectAdapter::createProxy(const Identity& ident) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    checkForDeactivation();
    checkIdentity(ident);

    return newProxy(ident, "");
}

#ifdef ICEE_HAS_LOCATOR
ObjectPrx
Ice::ObjectAdapter::createDirectProxy(const Identity& ident) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
    
    checkForDeactivation();
    checkIdentity(ident);

    return newDirectProxy(ident, "");
}

ObjectPrx
Ice::ObjectAdapter::createIndirectProxy(const Identity& ident) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
    
    checkForDeactivation();
    checkIdentity(ident);

    return newIndirectProxy(ident, "", _id);
}
#endif

ObjectPrx
Ice::ObjectAdapter::createReverseProxy(const Identity& ident) const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
    
    checkForDeactivation();
    checkIdentity(ident);

    //
    // Get all incoming connections for this object adapter.
    //
    vector<ConnectionPtr> connections;
    vector<IncomingConnectionFactoryPtr>::const_iterator p;
    for(p = _incomingConnectionFactories.begin(); p != _incomingConnectionFactories.end(); ++p)
    {
	list<ConnectionPtr> cons = (*p)->connections();
	copy(cons.begin(), cons.end(), back_inserter(connections));
    }

    //
    // Create a reference and return a reverse proxy for this
    // reference.
    //
    vector<EndpointPtr> endpoints;
    ReferencePtr ref = _instance->referenceFactory()->create(ident, _instance->initializationData().defaultContext, "",
    							     Reference::ModeTwoway, connections);
    return _instance->proxyFactory()->referenceToProxy(ref);
}

#ifdef ICEE_HAS_LOCATOR
void
Ice::ObjectAdapter::setLocator(const LocatorPrx& locator)
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
    
    checkForDeactivation();

    _locatorInfo = _instance->locatorManager()->get(locator);
}
#endif

void
Ice::ObjectAdapter::flushBatchRequests()
{
    vector<IncomingConnectionFactoryPtr> f;
    {
	IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
	f = _incomingConnectionFactories;
    }
    for_each(f.begin(), f.end(), Ice::voidMemFun(&IncomingConnectionFactory::flushBatchRequests));
}

void
Ice::ObjectAdapter::incDirectCount()
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);
 
    checkForDeactivation();

    assert(_directCount >= 0);
    ++_directCount;
}

void
Ice::ObjectAdapter::decDirectCount()
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    // Not check for deactivation here!

    assert(_instance); // Must not be called after waitForDeactivate().

    assert(_directCount > 0);
    if(--_directCount == 0)
    {
	notifyAll();
    }    
}

ServantManagerPtr
Ice::ObjectAdapter::getServantManager() const
{
    IceUtil::Monitor<IceUtil::RecMutex>::Lock sync(*this);

    return _servantManager;
}

Ice::ObjectAdapter::ObjectAdapter(const InstancePtr& instance, const CommunicatorPtr& communicator,
				  const ObjectAdapterFactoryPtr& objectAdapterFactory, 
				  const string& name, const string& endpointInfo
#ifdef ICEE_HAS_ROUTER
				  , const RouterPrx& router
#endif
				  ) :
    _deactivated(false),
    _instance(instance),
    _communicator(communicator),
    _objectAdapterFactory(objectAdapterFactory),
    _servantManager(new ServantManager(instance, name)),
    _printAdapterReadyDone(false),
    _name(name),
#ifdef ICEE_HAS_LOCATOR
    _id(instance->initializationData().properties->getProperty(name + ".AdapterId")),
    _replicaGroupId(instance->initializationData().properties->getProperty(name + ".ReplicaGroupId")),
#endif
    _directCount(0),
    _waitForDeactivate(false)
{
    __setNoDelete(true);
    try
    {
#ifdef ICEE_HAS_ROUTER
        if(!router)
        {
            string routerStr = _instance->initializationData().properties->getProperty(_name + ".Router");
            if(!routerStr.empty())
            {
                const_cast<RouterPrx&>(router) =
                    RouterPrx::uncheckedCast(_instance->proxyFactory()->stringToProxy(routerStr));
            }
        }
        if(router)
        {
            _routerInfo = _instance->routerManager()->get(router);
            if(_routerInfo)
            {
                //
                // Make sure this router is not already registered with another adapter.
                //
                if(_routerInfo->getAdapter())
                {
                    throw AlreadyRegisteredException(__FILE__, __LINE__, "object adapter with router",
                                                     _instance->identityToString(router->ice_getIdentity()));
                }

                //
                // Add the router's server proxy endpoints to this object
                // adapter.
                //
                ObjectPrx proxy = _routerInfo->getServerProxy();
                vector<EndpointPtr> endpoints = proxy->__reference()->getEndpoints();
                copy(endpoints.begin(), endpoints.end(), back_inserter(_routerEndpoints));
                sort(_routerEndpoints.begin(), _routerEndpoints.end()); // Must be sorted.
                _routerEndpoints.erase(unique(_routerEndpoints.begin(), _routerEndpoints.end()),
                                       _routerEndpoints.end());

                //
                // Associate this object adapter with the router. This way,
                // new outgoing connections to the router's client proxy will
                // use this object adapter for callbacks.
                //
                _routerInfo->setAdapter(this);

                //
                // Also modify all existing outgoing connections to the
                // router's client proxy to use this object adapter for
                // callbacks.
                //      
                _instance->outgoingConnectionFactory()->setRouterInfo(_routerInfo);
            }
	}
	else
#endif
	{
	    //
	    // Parse the endpoints, but don't store them in the adapter.
	    // The connection factory might change it, for example, to
	    // fill in the real port number.
	    //
	    vector<EndpointPtr> endpoints = parseEndpoints(endpointInfo);
	    for(vector<EndpointPtr>::iterator p = endpoints.begin(); p != endpoints.end(); ++p)
	    {
	        _incomingConnectionFactories.push_back(new IncomingConnectionFactory(_instance, *p, this));
	    }

	    //
	    // Parse published endpoints. These are used in proxies
	    // instead of the connection factory endpoints.
	    //
	    string endpts = _instance->initializationData().properties->getProperty(name + ".PublishedEndpoints");
	    _publishedEndpoints = parseEndpoints(endpts);
            if(_publishedEndpoints.empty())
            {
                transform(_incomingConnectionFactories.begin(), _incomingConnectionFactories.end(),
                          back_inserter(_publishedEndpoints), Ice::constMemFun(&IncomingConnectionFactory::endpoint));
            }

            //
            // Filter out any endpoints that are not meant to be published.
            //
            _publishedEndpoints.erase(remove_if(_publishedEndpoints.begin(), _publishedEndpoints.end(),
                                      not1(Ice::constMemFun(&Endpoint::publish))), _publishedEndpoints.end());
	}

#ifdef ICEE_HAS_LOCATOR
	string locator = _instance->initializationData().properties->getProperty(_name + ".Locator");
	if(!locator.empty())
	{
	    setLocator(LocatorPrx::uncheckedCast(_instance->proxyFactory()->stringToProxy(locator)));
	}
	else
	{
	    setLocator(_instance->referenceFactory()->getDefaultLocator());
	}
#endif
    }
    catch(...)
    {
	deactivate();
	waitForDeactivate();
	__setNoDelete(false);
	throw;
    }
    __setNoDelete(false);  
}

Ice::ObjectAdapter::~ObjectAdapter()
{
    if(!_deactivated)
    {
	Warning out(_instance->initializationData().logger);
	out << "object adapter `" << _name << "' has not been deactivated";
    }
    else if(_instance)
    {
	Warning out(_instance->initializationData().logger);
	out << "object adapter `" << _name << "' deactivation had not been waited for";
    }
    else
    {
	assert(!_servantManager);
	assert(!_communicator);
	assert(_incomingConnectionFactories.empty());
	assert(_directCount == 0);
	assert(!_waitForDeactivate);
    }
}

ObjectPrx
Ice::ObjectAdapter::newProxy(const Identity& ident, const string& facet) const
{
#ifdef ICEE_HAS_LOCATOR
    if(_id.empty())
    {
#endif
	return newDirectProxy(ident, facet);
#ifdef ICEE_HAS_LOCATOR
    }
    else if(_replicaGroupId.empty())
    {
	return newIndirectProxy(ident, facet, _id);
    }
    else
    {
	return newIndirectProxy(ident, facet, _replicaGroupId);
    }
#endif
}

ObjectPrx
Ice::ObjectAdapter::newDirectProxy(const Identity& ident, const string& facet) const
{
    vector<EndpointPtr> endpoints = _publishedEndpoints;
    
    //
    // Now we also add the endpoints of the router's server proxy, if
    // any. This way, object references created by this object adapter
    // will also point to the router's server proxy endpoints.
    //
#ifdef ICEE_HAS_ROUTER
    copy(_routerEndpoints.begin(), _routerEndpoints.end(), back_inserter(endpoints));
#endif
    
    //
    // Create a reference and return a proxy for this reference.
    //
#ifdef ICEE_HAS_ROUTER
    ReferencePtr ref = _instance->referenceFactory()->create(ident, _instance->initializationData().defaultContext,
    							     facet, Reference::ModeTwoway, false, endpoints, 0);
#else
    ReferencePtr ref = _instance->referenceFactory()->create(ident, _instance->initializationData().defaultContext,
    							     facet, Reference::ModeTwoway, false, endpoints);
#endif
    return _instance->proxyFactory()->referenceToProxy(ref);

}

#ifdef ICEE_HAS_LOCATOR
ObjectPrx
Ice::ObjectAdapter::newIndirectProxy(const Identity& ident, const string& facet, const string& id) const
{
    //
    // Create a reference with the adapter id.
    //
#ifdef ICEE_HAS_ROUTER
    ReferencePtr ref = _instance->referenceFactory()->create(ident, _instance->initializationData().defaultContext,
    							     facet, Reference::ModeTwoway, false, id, 0, _locatorInfo);
    
#else
    ReferencePtr ref = _instance->referenceFactory()->create(ident, _instance->initializationData().defaultContext,
    							     facet, Reference::ModeTwoway, false, id, _locatorInfo);
#endif

    //
    // Return a proxy for the reference. 
    //
    return _instance->proxyFactory()->referenceToProxy(ref);
}
#endif

void
Ice::ObjectAdapter::checkForDeactivation() const
{
    if(_deactivated)
    {
	ObjectAdapterDeactivatedException ex(__FILE__, __LINE__);
	ex.name = _name;
	throw ex;
    }
}

void
Ice::ObjectAdapter::checkIdentity(const Identity& ident)
{
    if(ident.name.size() == 0)
    {
        IllegalIdentityException e(__FILE__, __LINE__);
        e.id = ident;
        throw e;
    }
}

vector<EndpointPtr>
Ice::ObjectAdapter::parseEndpoints(const string& str) const
{
    string endpts = str;
    transform(endpts.begin(), endpts.end(), endpts.begin(), ::tolower);

    string::size_type beg;
    string::size_type end = 0;

    vector<EndpointPtr> endpoints;
    while(end < endpts.length())
    {
	const string delim = " \t\n\r";
	
	beg = endpts.find_first_not_of(delim, end);
	if(beg == string::npos)
	{
	    break;
	}

	end = endpts.find(':', beg);
	if(end == string::npos)
	{
	    end = endpts.length();
	}
	
	if(end == beg)
	{
	    ++end;
	    continue;
	}
	
	string s = endpts.substr(beg, end - beg);
	EndpointPtr endp = _instance->endpointFactory()->create(s);
	if(endp == 0)
	{
	    EndpointParseException ex(__FILE__, __LINE__);
	    ex.str = s;
	    throw ex;
	}
        vector<EndpointPtr> endps = endp->expand(true);
        endpoints.insert(endpoints.end(), endps.begin(), endps.end());

	++end;
    }

    return endpoints;
}
