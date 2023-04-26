/*!
 * @file ThermoIce0Temperature_test.cpp
 *
 * @date Apr 29, 2022
 * @author Tim Spain <timothy.spain@nersc.no>
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <sstream>

#include "include/ThermoIce0.hpp"
#include "include/IFluxCalculation.hpp"

#include "include/Configurator.hpp"
#include "include/ConfiguredModule.hpp"
#include "include/IFreezingPoint.hpp"
#include "include/IFreezingPointModule.hpp"
#include "include/ModelArray.hpp"
#include "include/ModelArrayRef.hpp"
#include "include/ModelComponent.hpp"
#include "include/Time.hpp"

namespace Nextsim {

TEST_SUITE_BEGIN("ThermoIce0");
TEST_CASE("Threshold ice")
{
    ModelArray::setDimensions(ModelArray::Type::H, { 1 });
    ModelArray::setDimensions(ModelArray::Type::Z, { 1, 1 });

    class IceTemperatureData : public ModelComponent {
    public:
        IceTemperatureData()
        {
            registerSharedArray(SharedArray::H_ICE, &hice);
            registerSharedArray(SharedArray::C_ICE, &cice);
            registerSharedArray(SharedArray::H_SNOW, &hsnow);
            registerProtectedArray(ProtectedArray::SST, &sst);
            registerProtectedArray(ProtectedArray::SSS, &sss);
            registerProtectedArray(ProtectedArray::TF, &tf);
            registerProtectedArray(ProtectedArray::SNOW, &snow);
            registerProtectedArray(ProtectedArray::ML_BULK_CP, &mlbhc);
            registerProtectedArray(ProtectedArray::T_ICE, &tice0);
            registerSharedArray(SharedArray::Q_IO, &qio);
        }
        std::string getName() const override { return "IceTemperatureData"; }

        void setData(const ModelState::DataMap&) override
        {
            cice[0] = 0.99;
            hice[0] = 0.001 / cice[0]; // Here we are using the true thicknesses
            hsnow[0] = 0.;
            sss[0] = 32.;
            sst[0] = Module::getImplementation<IFreezingPoint>()(sss[0]);
            snow[0] = 0.;
            tf[0] = Module::getImplementation<IFreezingPoint>()(sss[0]);
            mlbhc[0] = 4.29151e7;
            tice0[0] = -9.;
            qio[0] = 0.;
        }

        HField hice;
        HField cice;
        HField hsnow;
        HField sst;
        HField sss;
        HField tf;
        HField snow;
        HField mlbhc; // Mixed layer bulk heat capacity
        HField tice0;
        HField qio;

        ModelState getState() const override { return ModelState(); }
        ModelState getState(const OutputLevel&) const override { return getState(); }
    } atmoState;
    atmoState.setData(ModelState::DataMap());

    class FluxData : public IFluxCalculation {
    public:
        FluxData()
            : IFluxCalculation()
        {
        }
        std::string getName() const override { return "FluxData"; }

        void setData(const ModelState::DataMap&) override
        {
            qow[0] = 0;
            qia[0] = 0;
            dqia_dt[0] = 0;
            subl[0] = 0;
        }

        ModelState getState() const override { return ModelState(); }
        ModelState getState(const OutputLevel&) const override { return getState(); }

        void update(const TimestepTime&) override { }
    } fluxData;

    fluxData.setData(ModelState::DataMap());

    TimestepTime tst = { TimePoint("2000-01-01T00:00:00"), Duration(600) };
    ThermoIce0 ti0t;
    ti0t.configure();
    ti0t.setData(ModelState::DataMap());
    ti0t.update(tst);

    ModelArrayRef<ModelComponent::SharedArray::H_ICE, MARBackingStore> hice(ModelComponent::getSharedArray());
    // So little ice should be reduced to zero
    REQUIRE(hice[0] == 0.);
    ModelArrayRef<ModelComponent::SharedArray::C_ICE, MARBackingStore> cice(ModelComponent::getSharedArray());
    REQUIRE(cice[0] == 0.);

}
TEST_SUITE_END();
}
