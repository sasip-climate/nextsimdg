/*!
 * @file ERA5Atm_test.cpp
 *
 * @date Nov 25, 2022
 * @author Tim Spain <timothy.spain@nersc.no>
 */

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "include/ERA5Atmosphere.hpp"

namespace Nextsim {

TEST_CASE("ERA5Atmosphere construction test", "[ERA5Atmosphere]")
{
    std::string filePath = "ERA5test.nc";

    ERA5Atmosphere e5;
    e5.setFilePath(filePath);
}

}
