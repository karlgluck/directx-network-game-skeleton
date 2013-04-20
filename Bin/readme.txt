
                                   Network Game Skeleton

===============================================================================================
Copyright 2006-2010 Karl Gluck. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.

THIS SOFTWARE IS PROVIDED BY KARL GLUCK ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KARL GLUCK OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of Karl Gluck.
===============================================================================================


For best results, run the release versions (_Release) of the client and server.  Note that the
server must be running before the client will start successfully.  If you have problems, feel
free to ask for help on the forums at http://www.evitales.com
                                                                            -Karl Gluck



ngsclient_Debug.exe
ngsclient_Release.exe
    Two versions of the client executable, compiled with MSVC++ against the DirectX 9.0c library.

ngsserver_Debug.exe
ngsserver.Release.exe
    Two versions of the server executable that run Winsock 2.0 and accept packets on the
port 27192.  To use the network game skeleton over the internet, make sure this port is
forwarded from your external IP address (check this at whatismyip.com) by your router
and is not blocked by a firewall.  Internet clients must connect to your external IP
address, not the one that the application shows you when it runs (only use this if you are
running over a LAN)


grass.jpg
    Grass image, downloaded from a rights-free texture database

tiny\tiny_4anim.x
tiny\Tiny_skin.dds
    Microsoft DirectX SDK's animated human female "Tiny"