#!/usr/bin/env python
# **********************************************************************
#
# Copyright (c) 2003-2007 ZeroC, Inc. All rights reserved.
#
# This copy of Ice is licensed to you under the terms described in the
# ICE_LICENSE file included in this distribution.
#
# **********************************************************************

import sys, os

try:
    import demoscript
except ImportError:
    for toplevel in [".", "..", "../..", "../../..", "../../../.."]:
        toplevel = os.path.normpath(toplevel)
        if os.path.exists(os.path.join(toplevel, "demoscript")):
            break
    else:
        raise "can't find toplevel directory!"
    sys.path.append(os.path.join(toplevel))
    import demoscript

import demoscript.Util
demoscript.Util.defaultLanguage = "VB"
import demoscript.Glacier2.callback

server = demoscript.Util.spawn('./server.exe --Ice.PrintAdapterReady')
server.expect('.* ready')
sessionserver = demoscript.Util.spawn('./sessionserver.exe --Ice.PrintAdapterReady')
sessionserver.expect('.* ready')

glacier2 = demoscript.Util.spawn('glacier2router --Ice.Config=config.glacier2 --Ice.PrintAdapterReady --Glacier2.SessionTimeout=5', language="C++")
glacier2.expect('Glacier2.Client ready')
glacier2.expect('Glacier2.Server ready')

client = demoscript.Util.spawn('./client.exe')

demoscript.Glacier2.callback.run(client, server, sessionserver, glacier2)
