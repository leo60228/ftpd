// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2020 Michael Theall
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "platform.h"

#include "log.h"

#include <stdio.h>
#include <ogcsys.h>
#include <gccore.h>
#include <network.h>
#include <fat.h>

#include <cstring>

#ifndef CLASSIC
#error "Gamecube must be built in classic mode"
#endif

namespace
{
/// \brief Host address
struct in_addr s_addr = {0};
}

bool platform::networkVisible ()
{
	return true;
}

bool platform::networkAddress (SockAddr &addr_)
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr   = s_addr;

	addr_ = addr;
	return true;
}

bool platform::init ()
{
	void *framebuffer;

	struct in_addr netmask = {0};
	struct in_addr gateway = {0};

	VIDEO_Init();
	PAD_Init();
	
	fatInitDefault();

	GXRModeObj* rmode = VIDEO_GetPreferredMode(NULL);
	framebuffer = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(framebuffer,20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(framebuffer);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	printf("Configuring network...\n");
	s32 ret = if_configex(&s_addr, &netmask, &gateway, TRUE);
	if (ret >= 0)
	{
		printf("network configured\n");
	}
	else
	{
		printf("network configuration failed!\n");
	}

	return true;
}

bool platform::loop ()
{
	PAD_ScanPads();

	// check if the user wants to exit
	auto const kDown = PAD_ButtonsDown(0);
	if (kDown & PAD_BUTTON_START)
		return false;

	return true;
}

void platform::render ()
{
	VIDEO_WaitVSync();
}

void platform::exit ()
{

}
