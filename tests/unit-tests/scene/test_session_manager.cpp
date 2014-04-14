/*
 * Copyright © 2012-2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voss <thomas.voss@canonical.com>
 */

#include "src/server/scene/session_manager.h"
#include "mir/compositor/buffer_stream.h"
#include "src/server/scene/session_container.h"
#include "mir/shell/session.h"
#include "src/server/scene/surface_impl.h"
#include "mir/shell/session_listener.h"
#include "mir/shell/null_session_listener.h"
#include "mir/shell/surface_creation_parameters.h"
#include "src/server/scene/session_event_sink.h"
#include "src/server/scene/basic_surface.h"
#include "mir_test_doubles/null_event_sink.h"
#include "mir/shell/trust_session_creation_parameters.h"
#include "mir/shell/null_trust_session_listener.h"
#include "mir/shell/trust_session.h"

#include "mir_test/fake_shared.h"
#include "mir_test_doubles/mock_buffer_stream.h"
#include "mir_test_doubles/mock_surface_factory.h"
#include "mir_test_doubles/mock_focus_setter.h"
#include "mir_test_doubles/mock_session_listener.h"
#include "mir_test_doubles/stub_surface_builder.h"
#include "mir_test_doubles/stub_surface_ranker.h"
#include "mir_test_doubles/null_snapshot_strategy.h"
#include "mir_test_doubles/null_surface_configurator.h"
#include "mir_test_doubles/null_session_event_sink.h"
#include "mir_test_doubles/mock_trust_session_listener.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace mc = mir::compositor;
namespace mf = mir::frontend;
namespace msh = mir::shell;
namespace ms = mir::scene;
namespace geom = mir::geometry;
namespace mt = mir::test;
namespace mtd = mir::test::doubles;

namespace
{
struct MockSessionContainer : public ms::SessionContainer
{
    MOCK_METHOD1(insert_session, void(std::shared_ptr<msh::Session> const&));
    MOCK_METHOD1(remove_session, void(std::shared_ptr<msh::Session> const&));
    MOCK_CONST_METHOD1(successor_of, std::shared_ptr<msh::Session>(std::shared_ptr<msh::Session> const&));
    MOCK_CONST_METHOD1(for_each, void(std::function<void(std::shared_ptr<msh::Session> const&)>));
    MOCK_METHOD0(lock, void());
    MOCK_METHOD0(unlock, void());
    ~MockSessionContainer() noexcept {}
};

struct MockSessionEventSink : public ms::SessionEventSink
{
    MOCK_METHOD1(handle_focus_change, void(std::shared_ptr<msh::Session> const& session));
    MOCK_METHOD0(handle_no_focus, void());
    MOCK_METHOD1(handle_session_stopping, void(std::shared_ptr<msh::Session> const& session));
};

struct SessionManagerSetup : public testing::Test
{
    SessionManagerSetup()
      : session_manager(mt::fake_shared(surface_factory),
                        mt::fake_shared(container),
                        mt::fake_shared(focus_setter),
                        std::make_shared<mtd::NullSnapshotStrategy>(),
                        std::make_shared<mtd::NullSessionEventSink>(),
                        mt::fake_shared(session_listener),
                        std::make_shared<msh::NullTrustSessionListener>())
    {
        using namespace ::testing;
        ON_CALL(container, successor_of(_)).WillByDefault(Return((std::shared_ptr<msh::Session>())));
    }

    mtd::StubSurfaceBuilder surface_builder;
    mtd::StubSurfaceRanker surface_ranker;
    mtd::MockSurfaceFactory surface_factory;
    testing::NiceMock<MockSessionContainer> container;    // Inelegant but some tests need a stub
    testing::NiceMock<mtd::MockFocusSetter> focus_setter; // Inelegant but some tests need a stub
    msh::NullSessionListener session_listener;
    mtd::NullEventSink event_sink;

    ms::SessionManager session_manager;
};

}

TEST_F(SessionManagerSetup, open_and_close_session)
{
    using namespace ::testing;

    EXPECT_CALL(container, insert_session(_)).Times(1);
    EXPECT_CALL(container, remove_session(_)).Times(1);
    EXPECT_CALL(focus_setter, set_focus_to(_));
    EXPECT_CALL(focus_setter, set_focus_to(std::shared_ptr<msh::Session>())).Times(1);

    auto session = session_manager.open_session(__LINE__, "Visual Basic Studio", mt::fake_shared(event_sink));
    session_manager.close_session(session);
}

TEST_F(SessionManagerSetup, closing_session_removes_surfaces)
{
    using namespace ::testing;

    EXPECT_CALL(surface_factory, create_surface(_, _, _, _)).Times(1);

    ON_CALL(surface_factory, create_surface(_, _, _, _)).WillByDefault(
       Return(std::make_shared<ms::SurfaceImpl>(
           surface_builder.create_surface(mf::SurfaceId{}, msh::a_surface(), std::shared_ptr<mf::EventSink>(), std::make_shared<mtd::NullSurfaceConfigurator>()),
           mt::fake_shared(surface_builder))));

    EXPECT_CALL(container, insert_session(_)).Times(1);
    EXPECT_CALL(container, remove_session(_)).Times(1);

    EXPECT_CALL(focus_setter, set_focus_to(_)).Times(1);
    EXPECT_CALL(focus_setter, set_focus_to(std::shared_ptr<msh::Session>())).Times(1);

    auto session = session_manager.open_session(__LINE__, "Visual Basic Studio", mt::fake_shared(event_sink));
    session->create_surface(msh::a_surface().of_size(geom::Size{geom::Width{1024}, geom::Height{768}}));

    session_manager.close_session(session);
}

TEST_F(SessionManagerSetup, new_applications_receive_focus)
{
    using namespace ::testing;
    std::shared_ptr<msh::Session> new_session;

    EXPECT_CALL(container, insert_session(_)).Times(1);
    EXPECT_CALL(focus_setter, set_focus_to(_)).WillOnce(SaveArg<0>(&new_session));

    auto session = session_manager.open_session(__LINE__, "Visual Basic Studio", mt::fake_shared(event_sink));
    EXPECT_EQ(session, new_session);
}

TEST_F(SessionManagerSetup, create_surface_for_session_forwards_and_then_focuses_session)
{
    using namespace ::testing;
    ON_CALL(surface_factory, create_surface(_, _, _, _)).WillByDefault(
       Return(std::make_shared<ms::SurfaceImpl>(
           surface_builder.create_surface(mf::SurfaceId{}, msh::a_surface(), mt::fake_shared(event_sink), std::make_shared<mtd::NullSurfaceConfigurator>()),
           mt::fake_shared(surface_builder))));

    // Once for session creation and once for surface creation
    {
        InSequence seq;

        EXPECT_CALL(focus_setter, set_focus_to(_)).Times(1); // Session creation
        EXPECT_CALL(surface_factory, create_surface(_, _, _, _)).Times(1);
        EXPECT_CALL(focus_setter, set_focus_to(_)).Times(1); // Post Surface creation
    }

    auto session1 = session_manager.open_session(__LINE__, "Weather Report", mt::fake_shared(event_sink));
    session_manager.create_surface_for(session1, msh::a_surface());
}

namespace
{

struct SessionManagerSessionListenerSetup : public testing::Test
{
    SessionManagerSessionListenerSetup()
      : session_manager(mt::fake_shared(surface_factory),
                        mt::fake_shared(container),
                        mt::fake_shared(focus_setter),
                        std::make_shared<mtd::NullSnapshotStrategy>(),
                        std::make_shared<mtd::NullSessionEventSink>(),
                        mt::fake_shared(session_listener),
                        std::make_shared<msh::NullTrustSessionListener>())
    {
        using namespace ::testing;
        ON_CALL(container, successor_of(_)).WillByDefault(Return((std::shared_ptr<msh::Session>())));
    }

    mtd::MockSurfaceFactory surface_factory;
    testing::NiceMock<MockSessionContainer> container;    // Inelegant but some tests need a stub
    testing::NiceMock<mtd::MockFocusSetter> focus_setter; // Inelegant but some tests need a stub
    testing::NiceMock<mtd::MockSessionListener> session_listener;
    mtd::NullEventSink event_sink;

    ms::SessionManager session_manager;
};
}

TEST_F(SessionManagerSessionListenerSetup, session_listener_is_notified_of_lifecycle_and_focus)
{
    using namespace ::testing;

    EXPECT_CALL(session_listener, starting(_)).Times(1);
    EXPECT_CALL(session_listener, focused(_)).Times(1);
    EXPECT_CALL(session_listener, stopping(_)).Times(1);
    EXPECT_CALL(session_listener, unfocused()).Times(1);

    auto session = session_manager.open_session(__LINE__, "XPlane", mt::fake_shared(event_sink));
    session_manager.close_session(session);
}

namespace
{

struct SessionManagerSessionEventsSetup : public testing::Test
{
    SessionManagerSessionEventsSetup()
      : session_manager(mt::fake_shared(surface_factory),
                        mt::fake_shared(container),
                        mt::fake_shared(focus_setter),
                        std::make_shared<mtd::NullSnapshotStrategy>(),
                        mt::fake_shared(session_event_sink),
                        std::make_shared<msh::NullSessionListener>(),
                        std::make_shared<msh::NullTrustSessionListener>())
    {
        using namespace ::testing;
        ON_CALL(container, successor_of(_)).WillByDefault(Return((std::shared_ptr<msh::Session>())));
    }

    mtd::MockSurfaceFactory surface_factory;
    testing::NiceMock<MockSessionContainer> container;    // Inelegant but some tests need a stub
    testing::NiceMock<mtd::MockFocusSetter> focus_setter; // Inelegant but some tests need a stub
    MockSessionEventSink session_event_sink;
    mtd::NullEventSink event_sink;

    ms::SessionManager session_manager;
};

}

TEST_F(SessionManagerSessionEventsSetup, session_event_sink_is_notified_of_lifecycle_and_focus)
{
    using namespace ::testing;

    EXPECT_CALL(session_event_sink, handle_focus_change(_)).Times(2);

    auto session = session_manager.open_session(__LINE__, "XPlane", mt::fake_shared(event_sink));
    auto session1 = session_manager.open_session(__LINE__, "Bla", mt::fake_shared(event_sink));

    Mock::VerifyAndClearExpectations(&session_event_sink);

    InSequence s;
    EXPECT_CALL(session_event_sink, handle_session_stopping(_)).Times(1);
    EXPECT_CALL(container, successor_of(_)).
        WillOnce(Return(std::dynamic_pointer_cast<msh::Session>(session)));
    EXPECT_CALL(session_event_sink, handle_focus_change(_)).Times(1);
    EXPECT_CALL(session_event_sink, handle_session_stopping(_)).Times(1);
    EXPECT_CALL(container, successor_of(_)).
        WillOnce(Return(std::shared_ptr<msh::Session>()));
    EXPECT_CALL(session_event_sink, handle_no_focus()).Times(1);

    session_manager.close_session(session1);
    session_manager.close_session(session);
}

namespace
{

struct SessionManagerTrustSessionListenerSetup : public testing::Test
{
    SessionManagerTrustSessionListenerSetup()
      : session_manager(mt::fake_shared(surface_factory),
                        mt::fake_shared(container),
                        mt::fake_shared(focus_setter),
                        std::make_shared<mtd::NullSnapshotStrategy>(),
                        std::make_shared<mtd::NullSessionEventSink>(),
                        std::make_shared<msh::NullSessionListener>(),
                        mt::fake_shared(trust_session_listener))
    {
        using namespace ::testing;
        ON_CALL(container, successor_of(_)).WillByDefault(Return((std::shared_ptr<msh::Session>())));
    }

    mtd::MockSurfaceFactory surface_factory;
    testing::NiceMock<MockSessionContainer> container;    // Inelegant but some tests need a stub
    testing::NiceMock<mtd::MockFocusSetter> focus_setter; // Inelegant but some tests need a stub
    testing::NiceMock<mtd::MockTrustSessionListener> trust_session_listener;
    mtd::NullEventSink event_sink;

    ms::SessionManager session_manager;
};
}

TEST_F(SessionManagerTrustSessionListenerSetup, trust_session_listener_is_notified_of_trust_session_start_and_stop)
{
    using namespace ::testing;

    EXPECT_CALL(trust_session_listener, starting(_)).Times(1);
    EXPECT_CALL(trust_session_listener, stopping(_)).Times(1);

    auto helper = session_manager.open_session(__LINE__, "XPlane", mt::fake_shared(event_sink));

    std::string error;
    msh::TrustSessionCreationParameters parameters;

    auto trust_session = session_manager.start_trust_session_for(error, helper, parameters);
    session_manager.stop_trust_session(trust_session);
}
