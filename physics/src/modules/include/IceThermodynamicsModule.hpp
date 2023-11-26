/*!
 * @file IceThermodynamicsModule.hpp
 *
 * Generated by module_header.py
 */

#ifndef ICETHERMODYNAMICSMODULE_HPP
#define ICETHERMODYNAMICSMODULE_HPP

#include "include/ConfiguredModule.hpp"
#include "include/Module.hpp"

#include "IIceThermodynamics.hpp"

namespace Module {

template <> Module<Nextsim::IIceThermodynamics>::map Module<Nextsim::IIceThermodynamics>::functionMap;
class IceThermodynamicsModule : public Module<Nextsim::IIceThermodynamics> {
    struct Constructor {
        Constructor();
    };
    static Constructor ctor;
};

} /* namespace Module */

#endif /* ICETHERMODYNAMICSMODULE_HPP */
