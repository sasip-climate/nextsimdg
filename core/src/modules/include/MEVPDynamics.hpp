/*!
 * @file MEVPDynamics.hpp
 *
 * @date 27 Mar 2023
 * @author Tim Spain <timothy.spain@nersc.no>
 * @author Piotr Minakowski <piotr.minakowski@ovgu.de>
 */

#ifndef DYNAMICS_HPP
#define DYNAMICS_HPP

#include "include/MEVPDynamicsKernel.hpp"
#include "IDynamics.hpp"

#include "include/ModelArray.hpp"
#include "include/ModelComponent.hpp"

namespace Nextsim {
class MEVPDynamics : public IDynamics {
public:
    MEVPDynamics();

    std::string getName() const override { return "MEVPDynamics"; }
    void update(const TimestepTime& tst) override;

    void setData(const ModelState::DataMap&) override;
private:
    // TODO: How to get the template parameters here?
    MEVPDynamicsKernel<2, 6> kernel;
};
}

#endif /* DYNAMICS_HPP */
