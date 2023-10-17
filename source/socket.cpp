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

#include "socket.h"

#include "log.h"

#include <fcntl.h>
#ifdef GEKKO
#include <network.h>
#else
#include <sys/socket.h>
#endif
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

///////////////////////////////////////////////////////////////////////////
Socket::~Socket ()
{
	if (m_listening)
		info ("Stop listening on [%s]:%u\n", m_sockName.name (), m_sockName.port ());

	if (m_connected)
		info ("Closing connection to [%s]:%u\n", m_peerName.name (), m_peerName.port ());

#ifdef NDS
	if (::closesocket (m_fd) != 0)
		error ("closesocket: %s\n", std::strerror (errno));
#elif defined(GEKKO)
	int err = ::net_close (m_fd);
	if (err != 0)
		error ("net_close: %s\n", std::strerror (-err));
#else
	if (::close (m_fd) != 0)
		error ("close: %s\n", std::strerror (errno));
#endif
}

Socket::Socket (int const fd_) : m_fd (fd_), m_listening (false), m_connected (false)
{
}

Socket::Socket (int const fd_, SockAddr const &sockName_, SockAddr const &peerName_)
    : m_sockName (sockName_),
      m_peerName (peerName_),
      m_fd (fd_),
      m_listening (false),
      m_connected (true)
{
}

UniqueSocket Socket::accept ()
{
	SockAddr addr;
	socklen_t addrLen = sizeof (struct sockaddr_storage);

#ifdef GEKKO
	auto const fd = ::net_accept (m_fd, addr, &addrLen);
	if (fd < 0)
	{
		error ("net_accept: %s\n", std::strerror (-fd));
		return nullptr;
	}
#else
	auto const fd = ::accept (m_fd, addr, &addrLen);
	if (fd < 0)
	{
		error ("accept: %s\n", std::strerror (errno));
		return nullptr;
	}
#endif

	info ("Accepted connection from [%s]:%u\n", addr.name (), addr.port ());
	return UniqueSocket (new Socket (fd, m_sockName, addr));
}

int Socket::atMark ()
{
#if defined(NDS) || defined(GEKKO)
	errno = ENOSYS;
	return -1;
#else
	auto const rc = ::sockatmark (m_fd);

	if (rc < 0)
		error ("sockatmark: %s\n", std::strerror (errno));

	return rc;
#endif
}

bool Socket::bind (SockAddr const &addr_)
{
	int err;
	switch (static_cast<struct sockaddr_storage const &> (addr_).ss_family)
	{
	case AF_INET:
#ifdef GEKKO
		err = ::net_bind (m_fd, (struct sockaddr*) (const sockaddr*) addr_, sizeof (struct sockaddr_in));
		if (err != 0)
		{
			error ("net_bind: %s\n", std::strerror (-err));
			return false;
		}
#else
		if (::bind (m_fd, addr_, sizeof (struct sockaddr_in)) != 0)
		{
			error ("bind: %s\n", std::strerror (errno));
			return false;
		}
#endif
		break;

#ifndef NO_IPV6
	case AF_INET6:
		if (::bind (m_fd, addr_, sizeof (struct sockaddr_in6)) != 0)
		{
			error ("bind: %s\n", std::strerror (errno));
			return false;
		}
		break;
#endif

	default:
		errno = EINVAL;
		error ("bind: %s\n", std::strerror (errno));
		break;
	}

	if (addr_.port () == 0)
	{
		// get socket name due to request for ephemeral port
		socklen_t addrLen = sizeof (struct sockaddr_storage);
#ifdef GEKKO
		int err = ::net_getsockname (m_fd, m_sockName, &addrLen);
		if (err != 0)
			error ("net_getsockname: %s\n", std::strerror (-err));
#else
		if (::getsockname (m_fd, m_sockName, &addrLen) != 0)
			error ("getsockname: %s\n", std::strerror (errno));
#endif
	}
	else
		m_sockName = addr_;

	return true;
}

bool Socket::connect (SockAddr const &addr_)
{
#ifdef GEKKO
	int err = ::net_connect (m_fd, (struct sockaddr*) (const sockaddr*) addr_, sizeof (struct sockaddr_storage));
	if (err != 0)
	{
		if (errno != EINPROGRESS)
			error ("net_connect: %s\n", std::strerror (-err));
		else
		{
			m_peerName  = addr_;
			m_connected = true;
			info ("Connecting to [%s]:%u\n", addr_.name (), addr_.port ());
		}
		return false;
	}
#else
	if (::connect (m_fd, addr_, sizeof (struct sockaddr_storage)) != 0)
	{
		if (errno != EINPROGRESS)
			error ("connect: %s\n", std::strerror (errno));
		else
		{
			m_peerName  = addr_;
			m_connected = true;
			info ("Connecting to [%s]:%u\n", addr_.name (), addr_.port ());
		}
		return false;
	}
#endif

	m_peerName  = addr_;
	m_connected = true;
	info ("Connected to [%s]:%u\n", addr_.name (), addr_.port ());
	return true;
}

bool Socket::listen (int const backlog_)
{
#ifdef GEKKO
	int err = ::net_listen (m_fd, backlog_);
	if (err != 0)
	{
		error ("net_listen: %s\n", std::strerror (-err));
		return false;
	}
#else
	if (::listen (m_fd, backlog_) != 0)
	{
		error ("listen: %s\n", std::strerror (errno));
		return false;
	}
#endif

	m_listening = true;
	return true;
}

bool Socket::shutdown (int const how_)
{
#ifdef GEKKO
	int err = ::net_shutdown (m_fd, how_);
	if (err != 0)
	{
		error ("net_shutdown: %s\n", std::strerror (-err));
		return false;
	}
#else
	if (::shutdown (m_fd, how_) != 0)
	{
		error ("shutdown: %s\n", std::strerror (errno));
		return false;
	}
#endif

	return true;
}

bool Socket::setLinger (bool const enable_, std::chrono::seconds const time_)
{
#if defined(NDS) || defined(GEKKO)
	errno = ENOSYS;
	return -1;
#else
	struct linger linger;
	linger.l_onoff  = enable_;
	linger.l_linger = time_.count ();

	auto const rc = ::setsockopt (m_fd, SOL_SOCKET, SO_LINGER, &linger, sizeof (linger));
	if (rc != 0)
	{
		error ("setsockopt(SO_LINGER, %s, %lus): %s\n",
		    enable_ ? "on" : "off",
		    static_cast<unsigned long> (time_.count ()),
		    std::strerror (errno));
		return false;
	}

	return true;
#endif
}

bool Socket::setNonBlocking (bool const nonBlocking_)
{
#if defined(NDS) || defined(GEKKO)
	unsigned long enable = nonBlocking_;

#ifdef GEKKO
	auto const rc = ::net_ioctl (m_fd, FIONBIO, &enable);
	if (rc != 0)
	{
		error ("fcntl(FIONBIO, %d): %s\n", nonBlocking_, std::strerror (-rc));
		return false;
	}
#else
	auto const rc = ::ioctl (m_fd, FIONBIO, &enable);
	if (rc != 0)
	{
		error ("fcntl(FIONBIO, %d): %s\n", nonBlocking_, std::strerror (errno));
		return false;
	}
#endif
#else
	auto flags = ::fcntl (m_fd, F_GETFL, 0);
	if (flags == -1)
	{
		error ("fcntl(F_GETFL): %s\n", std::strerror (errno));
		return false;
	}

	if (nonBlocking_)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if (::fcntl (m_fd, F_SETFL, flags) != 0)
	{
		error ("fcntl(F_SETFL, %d): %s\n", flags, std::strerror (errno));
		return false;
	}
#endif

	return true;
}

bool Socket::setReuseAddress (bool const reuse_)
{
	int const reuse = reuse_;
#ifdef GEKKO
	int err = ::net_setsockopt (m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));
	if (err != 0)
	{
		error ("net_setsockopt(SO_REUSEADDR, %s): %s\n", reuse_ ? "yes" : "no", std::strerror (-err));
		return false;
	}
#else
	if (::setsockopt (m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse)) != 0)
	{
		error ("setsockopt(SO_REUSEADDR, %s): %s\n", reuse_ ? "yes" : "no", std::strerror (errno));
		return false;
	}
#endif

	return true;
}

bool Socket::setRecvBufferSize (std::size_t const size_)
{
	int const size = size_;
#ifdef GEKKO
	int err = ::net_setsockopt (m_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size));
	if (err != 0)
	{
		error ("net_setsockopt(SO_RCVBUF, %zu): %s\n", size_, std::strerror (-err));
		return false;
	}
#else
	if (::setsockopt (m_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size)) != 0)
	{
		error ("setsockopt(SO_RCVBUF, %zu): %s\n", size_, std::strerror (errno));
		return false;
	}
#endif

	return true;
}

bool Socket::setSendBufferSize (std::size_t const size_)
{
	int const size = size_;
#ifdef GEKKO
	int err = ::net_setsockopt (m_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size));
	if (err != 0)
	{
		error ("net_setsockopt(SO_SNDBUF, %zu): %s\n", size_, std::strerror (-err));
		return false;
	}
#else
	if (::setsockopt (m_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size)) != 0)
	{
		error ("setsockopt(SO_SNDBUF, %zu): %s\n", size_, std::strerror (errno));
		return false;
	}
#endif

	return true;
}

std::make_signed_t<std::size_t>
    Socket::read (void *const buffer_, std::size_t const size_, bool const oob_)
{
	assert (buffer_);
	assert (size_);

#ifdef GEKKO
	auto const rc = ::net_recv (m_fd, buffer_, size_, 0);
	if (rc < 0 && rc != -EWOULDBLOCK && rc != -EAGAIN)
		error ("net_recv: %s\n", std::strerror (-rc));
#else
	auto const rc = ::recv (m_fd, buffer_, size_, oob_ ? MSG_OOB : 0);
	if (rc < 0 && errno != EWOULDBLOCK)
		error ("recv: %s\n", std::strerror (errno));
#endif

	return rc;
}

std::make_signed_t<std::size_t> Socket::read (IOBuffer &buffer_, bool const oob_)
{
	assert (buffer_.freeSize () > 0);

	auto const rc = read (buffer_.freeArea (), buffer_.freeSize (), oob_);
	if (rc > 0)
		buffer_.markUsed (rc);

	return rc;
}

std::make_signed_t<std::size_t> Socket::write (void const *const buffer_, std::size_t const size_)
{
	assert (buffer_);
	assert (size_ > 0);

#ifdef GEKKO
	auto const rc = ::net_send (m_fd, buffer_, size_, 0);
	if (rc < 0 && rc != -EWOULDBLOCK && rc != -EAGAIN)
		error ("net_send: %s\n", std::strerror (-rc));
#else
	auto const rc = ::send (m_fd, buffer_, size_, 0);
	if (rc < 0 && errno != EWOULDBLOCK)
		error ("send: %s\n", std::strerror (errno));
#endif

	return rc;
}

std::make_signed_t<std::size_t> Socket::write (IOBuffer &buffer_)
{
	assert (buffer_.usedSize () > 0);

	auto const rc = write (buffer_.usedArea (), buffer_.usedSize ());
	if (rc > 0)
		buffer_.markFree (rc);

	return rc;
}

SockAddr const &Socket::sockName () const
{
	return m_sockName;
}

SockAddr const &Socket::peerName () const
{
	return m_peerName;
}

UniqueSocket Socket::create ()
{
#ifdef GEKKO
	auto const fd = ::net_socket (AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		error ("net_socket: %s\n", std::strerror (-fd));
		return nullptr;
	}
#else
	auto const fd = ::socket (AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		error ("socket: %s\n", std::strerror (errno));
		return nullptr;
	}
#endif

	return UniqueSocket (new Socket (fd));
}

int Socket::poll (PollInfo *const info_,
    std::size_t const count_,
    std::chrono::milliseconds const timeout_)
{
	if (count_ == 0)
		return 0;

	auto const pfd = std::make_unique<struct pollfd[]> (count_);
	for (std::size_t i = 0; i < count_; ++i)
	{
		pfd[i].fd      = info_[i].socket.get ().m_fd;
		pfd[i].events  = info_[i].events;
		pfd[i].revents = 0;
	}

	auto const rc = ::poll (pfd.get (), count_, timeout_.count ());
	if (rc < 0)
	{
		error ("poll: %s\n", std::strerror (errno));
		return rc;
	}

	for (std::size_t i = 0; i < count_; ++i)
		info_[i].revents = pfd[i].revents;

	return rc;
}

#if defined(NDS) || defined(GEKKO)
extern "C" int poll (struct pollfd *const fds_, nfds_t const nfds_, int const timeout_)
{
	fd_set readFds;
	fd_set writeFds;
	fd_set exceptFds;

	FD_ZERO (&readFds);
	FD_ZERO (&writeFds);
	FD_ZERO (&exceptFds);

	for (nfds_t i = 0; i < nfds_; ++i)
	{
		if (fds_[i].events & POLLIN)
			FD_SET (fds_[i].fd, &readFds);
		if (fds_[i].events & POLLOUT)
			FD_SET (fds_[i].fd, &writeFds);
	}

	struct timeval tv;
	tv.tv_sec     = timeout_ / 1000;
	tv.tv_usec    = (timeout_ % 1000) * 1000;
#ifdef GEKKO
	auto const rc = ::net_select (nfds_, &readFds, &writeFds, &exceptFds, &tv);
#else
	auto const rc = ::select (nfds_, &readFds, &writeFds, &exceptFds, &tv);
#endif
	if (rc < 0)
		return rc;

	int count = 0;
	for (nfds_t i = 0; i < nfds_; ++i)
	{
		bool counted    = false;
		fds_[i].revents = 0;

		if (FD_ISSET (fds_[i].fd, &readFds))
		{
			counted = true;
			fds_[i].revents |= POLLIN;
		}

		if (FD_ISSET (fds_[i].fd, &writeFds))
		{
			counted = true;
			fds_[i].revents |= POLLOUT;
		}

		if (FD_ISSET (fds_[i].fd, &exceptFds))
		{
			counted = true;
			fds_[i].revents |= POLLERR;
		}

		if (counted)
			++count;
	}

	return count;
}
#endif
