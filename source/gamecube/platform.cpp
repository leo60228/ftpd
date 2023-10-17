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
#include <unistd.h>
#include <ogcsys.h>
#include <gccore.h>
#include <network.h>
#include <fat.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>

#include <cstring>
#include <cassert>

#ifndef CLASSIC
#error "Gamecube must be built in classic mode"
#endif

namespace
{
/// \brief Thread stack size
constexpr auto STACK_SIZE = 0x8000;

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
	VIDEO_SetBlack(false);
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

///////////////////////////////////////////////////////////////////////////
/// \brief Platform thread pimpl
class platform::Thread::privateData_t
{
public:
	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : thread (0), func (std::move (func_))
	{
		s32 ret = LWP_CreateThread (&thread, &privateData_t::threadFunc, this, NULL, STACK_SIZE, LWP_PRIO_NORMAL);
		assert (!ret);
	}

	/// \brief Underlying thread entry point
	/// \param arg_ Thread pimpl object
	static void *threadFunc (void *arg_)
	{
		// call passed-in entry point
		auto const t = static_cast<privateData_t *> (arg_);
		t->func ();
		return NULL;
	}

	/// \brief Underlying thread
	lwp_t thread = 0;
	/// \brief Thread entry point
	std::function<void ()> func;
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> &&func_)
    : m_d (new privateData_t (std::move (func_)))
{
}

platform::Thread::Thread (Thread &&that_) : m_d (new privateData_t ())
{
	std::swap (m_d, that_.m_d);
}

platform::Thread &platform::Thread::operator= (Thread &&that_)
{
	std::swap (m_d, that_.m_d);
	return *this;
}

void platform::Thread::join ()
{
	LWP_JoinThread (m_d->thread, NULL);
}

void platform::Thread::sleep (std::chrono::milliseconds const timeout_)
{
	usleep (std::chrono::nanoseconds (timeout_).count ());
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform mutex pimpl
class platform::Mutex::privateData_t
{
public:
	/// \brief Underlying mutex
	mutex_t mutex;
};

///////////////////////////////////////////////////////////////////////////
platform::Mutex::~Mutex ()
{
	LWP_MutexDestroy (m_d->mutex);
}

platform::Mutex::Mutex () : m_d (new privateData_t ())
{
	LWP_MutexInit (&m_d->mutex, false);
}

void platform::Mutex::lock ()
{
	LWP_MutexLock (m_d->mutex);
}

void platform::Mutex::unlock ()
{
	LWP_MutexUnlock (m_d->mutex);
}
