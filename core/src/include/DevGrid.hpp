/*!
 * @file DevGrid.hpp
 *
 * @date Dec 20, 2021
 * @author Tim Spain <timothy.spain@nersc.no>
 */

#ifndef CORE_SRC_INCLUDE_DEVGRID_HPP
#define CORE_SRC_INCLUDE_DEVGRID_HPP

#include "include/IStructure.hpp"

#include "include/PrognosticData.hpp"

#include <map>

namespace Nextsim {

class DevGrid : public IStructure {
public:
    DevGrid();
    virtual ~DevGrid();

    void init(netCDF::NcGroup& grp) override;
    void dumpMeta(netCDF::NcGroup& metaGroup) const override;
    void dumpData(netCDF::NcGroup& dataGroup) const override;

private:
    const static std::string ourStructureName;
    const static std::string xDimName;
    const static std::string yDimName;
    const static int nx;

    // pointer to a member of PrognosticData that takes no arguments and returns a
    // double. See https://isocpp.org/wiki/faq/pointers-to-members#typedef-for-ptr-to-memfn
    typedef double (PrognosticData::*ProgDoubleFn) () const;

    // Map between variable names and retrieval functions
    static const std::map<std::string, ProgDoubleFn> variableFunctions;

};

} /* namespace Nextsim */

#endif /* CORE_SRC_INCLUDE_DEVGRID_HPP */
