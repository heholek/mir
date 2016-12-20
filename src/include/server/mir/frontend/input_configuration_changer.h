/*
 * Copyright © 2016 Canonical Ltd.
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
 * Authored by: Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#ifndef MIR_FRONTEND_INPUT_CONFIGURATION_CHANGER_H_
#define MIR_FRONTEND_INPUT_CONFIGURATION_CHANGER_H_

#include <memory>

class MirInputConfiguration;
namespace mir
{
namespace frontend
{
class Session;

class InputConfigurationChanger
{
public:
    InputConfigurationChanger() = default;
    virtual ~InputConfigurationChanger() = default;

    virtual MirInputConfiguration base_configuration() = 0;
    virtual void configure(std::shared_ptr<Session> const&, MirInputConfiguration &&) = 0;
    virtual void set_base_configuration(MirInputConfiguration &&) = 0;

protected:
    InputConfigurationChanger(InputConfigurationChanger const& cp) = delete;
    InputConfigurationChanger& operator=(InputConfigurationChanger const& cp) = delete;

};

}
}

#endif

