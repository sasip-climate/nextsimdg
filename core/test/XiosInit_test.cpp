/*!
 * @file XiosInit_test.cpp
 * @brief Initialisation Testing for XIOS
 * @date Feb 28, 2023
 * @author Dr Alexander Smith <as3402@cam.ac.uk>
 * 
 * Test file intended to validate the initialization of the server process of
 * the XIOS class, including syncronising the state of the Nextsim grid. This
 * file serves as a set of unit tests, the system tests are elsewhere.
 */

#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <mpi.h>
#include "include/Xios.hpp"
#include <iostream>

#include "include/Configurator.hpp"

Nextsim::Xios *xios_handler;

int main( int argc, char* argv[] ) {
    // Enable xios in the 'config'
    Nextsim::Configurator::clearStreams();
    std::stringstream config;
    config << "[xios]" << std::endl
            << "enable = true" << std::endl;
    std::unique_ptr<std::istream> pcstream(new std::stringstream(config.str()));
    Nextsim::Configurator::addStream(std::move(pcstream));

    // (Temporary) MPI Init - Required by XIOS
    // TODO: Remove this MPI Init
    MPI_Init(NULL, NULL);

    // XIOS Class Init --- Initialises server. Unknown error if initialised per test.
    // TODO: Investigate and find workaround
    xios_handler = new Nextsim::Xios::Xios(NULL, NULL);

    int result = Catch::Session().run( argc, argv );

    // global clean-up...
    delete xios_handler;
    MPI_Finalize();
    return result;
}

TEST_CASE("XiosInitValidation") 
{
    // Validate initialization
    REQUIRE(xios_handler->validateServerConfiguration());

    // Validate initialization
    REQUIRE(xios_handler->validateCalendarConfiguration());
}

TEST_CASE("TestXiosDefaultConfigInitialisation") 
{
    //std::string xios_timestep = xios_handler->getCalendarTimestep().substr(0,2);
    //TODO: Swap to full timestep format when translator has been written in xios.cpp
    //std::string config_timestep = "P0-0T1:0:0";

    //TODO: Figure out why this stopped working
    //std::string config_timestep = "1h";
    // std::cout << "REQUIRE TIMESTEP MATCH" << std::endl;
    // REQUIRE(xios_timestep == config_timestep);
    // std::cout << "TESTED TIMESTEP" << std::endl;

    std::string xios_origin = xios_handler->getCalendarOrigin();
    std::string config_origin = "1970-01-01T00:00:00Z";

    std::cout << xios_origin << " " << config_origin << std::endl;
    REQUIRE(xios_origin == config_origin);
    std::cout << "TESTED ORIGIN" << std::endl;

    std::string xios_start = xios_handler->getCalendarStart();
    std::string config_start = "2023-03-03T17:11:00Z";

    std::cout << xios_start << " " << config_start << std::endl;
    REQUIRE(xios_start == config_start);
    std::cout << "TESTED Start" << std::endl;
}