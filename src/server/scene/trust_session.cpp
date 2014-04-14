/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored By: Nick Dedekind <nick.dedekind@canonical.com>
 */

#include "trust_session.h"
#include "mir/shell/trust_session_creation_parameters.h"
#include "mir/shell/session.h"
#include "mir/shell/trust_session_listener.h"
#include "session_container.h"

#include <sstream>
#include <algorithm>

namespace ms = mir::scene;
namespace msh = mir::shell;

int next_unique_id = 0;

ms::TrustSession::TrustSession(
    std::weak_ptr<msh::Session> const& session,
    msh::TrustSessionCreationParameters const&,
    std::shared_ptr<shell::TrustSessionListener> const& trust_session_listener) :
    trusted_helper(session),
    trust_session_listener(trust_session_listener),
    state(mir_trust_session_state_stopped)
{
}

ms::TrustSession::~TrustSession()
{
    TrustSession::stop();
}

MirTrustSessionState ms::TrustSession::get_state() const
{
    std::lock_guard<decltype(mutex)> lock(mutex);

    return state;
}

std::weak_ptr<msh::Session> ms::TrustSession::get_trusted_helper() const
{
    std::lock_guard<decltype(mutex)> lock(mutex);

    return trusted_helper;
}

void ms::TrustSession::start()
{
    std::lock_guard<decltype(mutex)> lock(mutex);

    if (state == mir_trust_session_state_started)
        return;

    state = mir_trust_session_state_started;

    auto helper = trusted_helper.lock();
    if (helper) {
        helper->begin_trust_session();
    }
}

void ms::TrustSession::stop()
{
    std::lock_guard<decltype(mutex)> lock(mutex);

    if (state == mir_trust_session_state_stopped)
        return;

    state = mir_trust_session_state_stopped;

    auto helper = trusted_helper.lock();
    if (helper) {
        helper->end_trust_session();
    }

    std::lock_guard<decltype(mutex_children)> child_lock(mutex_children);

    for (auto rit = trusted_children.rbegin(); rit != trusted_children.rend(); ++rit)
    {
        auto session = (*rit).lock();
        if (session)
        {
            session->end_trust_session();
            trust_session_listener->trusted_session_ending(*this, session);
        }
    }

    trusted_children.clear();
}

void ms::TrustSession::for_each_trusted_client_process(std::function<void(pid_t pid)>, bool) const
{
}

bool ms::TrustSession::add_trusted_child(std::shared_ptr<msh::Session> const& session)
{
    std::lock_guard<decltype(mutex)> lock(mutex);

    if (state == mir_trust_session_state_stopped)
        return false;

    std::lock_guard<decltype(mutex_children)> child_lock(mutex_children);

    if (std::find_if(trusted_children.begin(), trusted_children.end(),
            [session](std::weak_ptr<shell::Session> const& child)
            {
                return child.lock() == session;
            }) != trusted_children.end())
    {
        return false;
    }

    trusted_children.push_back(session);

    session->begin_trust_session();
    trust_session_listener->trusted_session_beginning(*this, session);
    return true;
}

void ms::TrustSession::remove_trusted_child(std::shared_ptr<msh::Session> const& session)
{
    std::lock_guard<decltype(mutex)> lock(mutex);

    if (state == mir_trust_session_state_stopped)
        return;

    std::lock_guard<decltype(mutex_children)> child_lock(mutex_children);

    for (auto it = trusted_children.begin(); it != trusted_children.end(); ++it)
    {
        auto trusted_child = (*it).lock();
        if (trusted_child && trusted_child == session) {

            trusted_children.erase(it);

            session->end_trust_session();
            trust_session_listener->trusted_session_ending(*this, session);
            break;
        }
    }
}

void ms::TrustSession::for_each_trusted_child(
    std::function<void(std::shared_ptr<msh::Session> const&)> f,
    bool reverse) const
{
    std::lock_guard<decltype(mutex_children)> child_lock(mutex_children);

    if (reverse)
    {
        for (auto rit = trusted_children.rbegin(); rit != trusted_children.rend(); ++rit)
        {
            if (auto trusted_child = (*rit).lock())
                f(trusted_child);
        }
    }
    else
    {
        for (auto it = trusted_children.begin(); it != trusted_children.end(); ++it)
        {
            if (auto trusted_child = (*it).lock())
                f(trusted_child);
        }
    }
}
