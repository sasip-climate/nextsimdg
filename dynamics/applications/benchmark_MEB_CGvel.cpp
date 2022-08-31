/*!
 * @file benchmark_MEB_CGvel.cpp
 * @date 1 Mar 2022
 * @author Piotr Minakowski <piotr.minakowski@ovgu.de>
 */

#include "MEB.hpp"
#include "MEBSandbox.hpp"
#include "Tools.hpp"
#include "cgMomentum.hpp"
#include "cgVector.hpp"
#include "dgInitial.hpp"
#include "dgTransport.hpp"
#include "dgVisu.hpp"
#include "mevp.hpp"
#include "stopwatch.hpp"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <vector>

bool WRITE_VTK = true;

#define CG 1
#define DGadvection 1
#define DGstress 1

namespace Nextsim {
extern Timer GlobalTimer;
}

inline constexpr double SQR(double x) { return x * x; }

//! Description of the problem data, wind & ocean fields
struct OceanX {
public:
    double operator()(double x, double y) const
    {
        return RefScale::vmax_ocean * (2.0 * y / RefScale::L - 1.0);
    }
};
struct OceanY {
public:
    double operator()(double x, double y) const
    {
        return RefScale::vmax_ocean * (1.0 - 2.0 * x / RefScale::L);
    }
};

struct AtmX {
    double time;

public:
    void settime(double t) { time = t; }
    double operator()(double x, double y) const
    {
        constexpr double oneday = 24.0 * 60.0 * 60.0;
        //! Center of cyclone (in m)
        double cM = 256000. + 51200. * time / oneday;

        //! scaling factor to reduce wind away from center
        double scale = exp(1.0) / 100.0 * exp(-0.01e-3 * sqrt(SQR(x - cM) + SQR(y - cM))) * 1.e-3;

        double alpha = 72.0 / 180.0 * M_PI;
        return -scale * RefScale::vmax_atm * (cos(alpha) * (x - cM) + sin(alpha) * (y - cM));
    }
};
struct AtmY {
    double time;

public:
    void settime(double t) { time = t; }
    double operator()(double x, double y) const
    {
        constexpr double oneday = 24.0 * 60.0 * 60.0;
        //! Center of cyclone (in m)
        double cM = 256000. + 51200. * time / oneday;

        //! scaling factor to reduce wind away from center
        double scale = exp(1.0) / 100.0 * exp(-0.01e-3 * sqrt(SQR(x - cM) + SQR(y - cM))) * 1.e-3;

        double alpha = 72.0 / 180.0 * M_PI;
        return -scale * RefScale::vmax_atm * (-sin(alpha) * (x - cM) + cos(alpha) * (y - cM));
    }
};
struct InitialH {
public:
    double operator()(double x, double y) const
    {
        return 0.3 + 0.005 * (sin(6.e-5 * x) + sin(3.e-5 * y));
    }
};
struct InitialA {
public:
    double operator()(double x, double y) const { return 1.0; }
};
struct InitialD {
public:
    double operator()(double x, double y) const { return 0.0; }
};

int main()
{
    //!

    Nextsim::CGMomentum momentum;

    //! Define the spatial mesh
    Nextsim::Mesh mesh;
    constexpr size_t N = 128; //!< Number of mesh nodes
    mesh.BasicInit(N, N, RefScale::L / N, RefScale::L / N);
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << "Spatial mesh with mesh " << N << " x " << N << " elements." << std::endl;

    //! define the time mesh
    constexpr double dt_adv = 12.0; //!< Time step of advection problem
    constexpr size_t NT = RefScale::T / dt_adv + 1.e-4; //!< Number of Advections steps

    constexpr size_t mom_substeps = 100;
    constexpr double dt_momentum = dt_adv / mom_substeps; //!< Time step of momentum problem

    std::cout << "Time step size (advection) " << dt_adv << "\t" << NT << " time steps" << std::endl
              << "Momentum subcycling " << mom_substeps << "\t dt_momentum " << dt_momentum
              << std::endl;

    //! VTK output
    constexpr double T_vtk = 4. * 60.0 * 60.0; // evey 4 hours
    constexpr size_t NT_vtk = T_vtk / dt_adv + 1.e-4;
    //! LOG message
    constexpr double T_log = 10.0 * 60.0; // every 30 minute
    constexpr size_t NT_log = T_log / dt_adv + 1.e-4;

    //! Variables
    Nextsim::CGVector<CG> vx(mesh), vy(mesh); //!< velocity
    Nextsim::CGVector<CG> tmpx(mesh), tmpy(mesh); //!< tmp for stress.. should be removed
    Nextsim::CGVector<CG> cg_A(mesh), cg_H(mesh); //!< interpolation of ice height and conc.
    // Nextsim::CGVector<CG> vx_mevp(mesh), vy_mevp(mesh); //!< temp. Velocity used for MEVP
    vx.zero();
    vy.zero();

    Nextsim::CGVector<CG> OX(mesh); //!< x-component of ocean velocity
    Nextsim::CGVector<CG> OY(mesh); //!< y-component of ocean velocity
    Nextsim::InterpolateCG(mesh, OX, OceanX());
    Nextsim::InterpolateCG(mesh, OY, OceanY());
    Nextsim::CGVector<CG> AX(mesh); //!< x-component of atm. velocity
    Nextsim::CGVector<CG> AY(mesh); //!< y-component of atm. velocity
    AtmX AtmForcingX;
    AtmY AtmForcingY;
    AtmForcingX.settime(0.0);
    AtmForcingY.settime(0.0);
    AX.zero();
    AY.zero();

    Nextsim::CellVector<DGadvection> H(mesh), A(mesh); //!< ice height and concentration
    Nextsim::L2ProjectInitial(mesh, H, InitialH());
    Nextsim::L2ProjectInitial(mesh, A, InitialA());
    Nextsim::CellVector<DGstress> E11(mesh), E12(mesh), E22(mesh); //!< storing strain rates
    Nextsim::CellVector<DGstress> S11(mesh), S12(mesh), S22(mesh); //!< storing stresses rates

    Nextsim::CellVector<1> DELTA(mesh); //!< Storing DELTA
    Nextsim::CellVector<1> SHEAR(mesh); //!< Storing DELTA
    Nextsim::CellVector<1> S1(mesh), S2(mesh); //!< Stress invariants

    // Temporary variables
    Nextsim::CellVector<1> eta1(mesh), eta2(mesh);
    Nextsim::CellVector<1> MU1(mesh), MU2(mesh);
    Nextsim::CellVector<1> stressrelax(mesh);
    Nextsim::CellVector<1> sigma_outside(mesh);
    Nextsim::CellVector<1> tildeP(mesh);
    Nextsim::CellVector<1> Pmax(mesh);
    Nextsim::CellVector<1> td(mesh);
    Nextsim::CellVector<1> d_crit(mesh);
    Nextsim::CellVector<1> tau(mesh);
    Nextsim::CellVector<1> sigma_n(mesh);
    Nextsim::CellVector<1> Regime(mesh);
    Nextsim::CellVector<1> Multip(mesh);
    Nextsim::CellVector<1> Lambda(mesh);

    Nextsim::CellVector<DGadvection> D(mesh); //!< ice damage. ?? Really dG(0) ??
    Nextsim::L2ProjectInitial(mesh, D, InitialD());

    //! Transport
    Nextsim::CellVector<DGadvection> dgvx(mesh), dgvy(mesh);
    Nextsim::DGTransport<DGadvection, DGadvection> dgtransport(dgvx, dgvy);
    dgtransport.settimesteppingscheme("rk1");
    dgtransport.setmesh(mesh);

    // save initial condition
    Nextsim::GlobalTimer.start("time loop - i/o");
    Nextsim::VTK::write_cg("ResultsMEB_CGvel/vx", 0, vx, mesh);
    Nextsim::VTK::write_cg("ResultsMEB_CGvel/vy", 0, vy, mesh);
    Nextsim::Tools::Delta(mesh, E11, E12, E22, RefScale::DeltaMin, DELTA);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/Delta", 0, DELTA, mesh);
    Nextsim::Tools::Shear(mesh, E11, E12, E22, RefScale::DeltaMin, SHEAR);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/Shear", 0, SHEAR, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/A", 0, A, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/H", 0, H, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/D", 0, D, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/S11", 0, S11, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/S12", 0, S12, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/S22", 0, S22, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/dvx", 0, dgvx, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/dvy", 0, dgvy, mesh);

    Nextsim::Tools::ElastoParams(
        mesh, E11, E12, E22, H, A, RefScale::DeltaMin, RefScale::Pstar, MU1, MU2);

    Nextsim::VTK::write_dg("ResultsMEB_CGvel/mu1", 0, MU1, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/mu2", 0, MU2, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/eta1", 0, eta1, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/eta2", 0, eta2, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/stressrelax", 0, stressrelax, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/stressOutside", 0, sigma_outside, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/Pmax", 0, Pmax, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/sigma_n", 0, sigma_n, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/tau", 0, tau, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/td", 0, td, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/d_crit", 0, d_crit, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/Regime", 0, Regime, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/Multip", 0, Multip, mesh);
    Nextsim::VTK::write_dg("ResultsMEB_CGvel/Lambda", 0, Lambda, mesh);

    Nextsim::GlobalTimer.stop("time loop - i/o");

    //! Initial Forcing
    AtmForcingX.settime(0);
    AtmForcingY.settime(0);
    Nextsim::InterpolateCG(mesh, AX, AtmForcingX);
    Nextsim::InterpolateCG(mesh, AY, AtmForcingY);

    Nextsim::GlobalTimer.start("time loop");

    for (size_t timestep = 1; timestep <= NT; ++timestep) {
        double time = dt_adv * timestep;
        double timeInMinutes = time / 60.0;
        double timeInHours = time / 60.0 / 60.0;
        double timeInDays = time / 60.0 / 60.0 / 24.;

        if (timestep % NT_log == 0)
            std::cout << "\rAdvection step " << timestep << "\t " << std::setprecision(2)
                      << std::fixed << std::setw(10) << std::right << time << "s\t" << std::setw(8)
                      << std::right << timeInMinutes << "m\t" << std::setw(6) << std::right
                      << timeInHours << "h\t" << std::setw(6) << std::right << timeInDays << "d\t\t"
                      << std::flush;

        //! Initialize time-dependent forcing
        Nextsim::GlobalTimer.start("time loop - forcing");
        AtmForcingX.settime(time);
        AtmForcingY.settime(time);
        Nextsim::InterpolateCG(mesh, AX, AtmForcingX);
        Nextsim::InterpolateCG(mesh, AY, AtmForcingY);
        Nextsim::GlobalTimer.stop("time loop - forcing");

        Nextsim::GlobalTimer.start("time loop - advection");
        momentum.ProjectCGToDG(mesh, dgvx, vx);
        momentum.ProjectCGToDG(mesh, dgvy, vy);
        dgtransport.reinitvelocity();
        dgtransport.step(dt_adv, A);
        dgtransport.step(dt_adv, H);
        dgtransport.step(dt_adv, D);

        A.col(0) = A.col(0).cwiseMin(1.0);
        A.col(0) = A.col(0).cwiseMax(0.0);
        H.col(0) = H.col(0).cwiseMax(0.0);
        D.col(0) = D.col(0).cwiseMin(1.0);
        D.col(0) = D.col(0).cwiseMax(0.0);

        momentum.InterpolateDGToCG(mesh, cg_A, A);
        momentum.InterpolateDGToCG(mesh, cg_H, H);

        cg_A = cg_A.cwiseMin(1.0);
        cg_A = cg_A.cwiseMax(0.0);
        cg_H = cg_H.cwiseMax(1.e-4); //!< Limit H from below

        Nextsim::GlobalTimer.stop("time loop - advection");

        Nextsim::GlobalTimer.start("time loop - meb");

        //! Momentum subcycling
        for (size_t mom_step = 0; mom_step < mom_substeps; ++mom_step) {

            Nextsim::GlobalTimer.start("time loop - meb - strain");
            //! Compute Strain Rate
            momentum.ProjectCG2VelocityToDG1Strain(mesh, E11, E12, E22, vx, vy);
            Nextsim::GlobalTimer.stop("time loop - meb - strain");

            Nextsim::GlobalTimer.start("time loop - meb - stress");
            //! Stress Update

            // Nextsim::MEB::StressUpdate(mesh, S11, S12, S22, E11, E12, E22,
            //     H, A, D, dt_momentum);

            // Nextsim::MEB::StressUpdateSandbox(mesh, S11, S12, S22, E11, E12, E22, H, A, D, DELTA,
            //     SHEAR, S1, S2, eta1, eta2, stressrelax, sigma_outside, tildeP, Pmax, td, d_crit,
            //     Regime, Multip, Lambda, dt_momentum);

            Nextsim::MEBSandbox::StressUpdateVP(mesh, S11, S12, S22, E11, E12, E22,
                H, A, RefScale::Pstar, RefScale::DeltaMin, dt_momentum);

            Nextsim::GlobalTimer.stop("time loop - meb - stress");

            Nextsim::GlobalTimer.start("time loop - meb - update");
            //! Update
            Nextsim::GlobalTimer.start("time loop - meb - update1");

            // update by a loop.. explicit parts and h-dependent
            vx = (1.0
                / (RefScale::rho_ice * cg_H.array() / dt_momentum // implicit parts
                    + cg_A.array() * RefScale::F_ocean
                        * (OX.array() - vx.array()).abs()) // implicit parts
                * (RefScale::rho_ice * cg_H.array() / dt_momentum * vx.array() + //
                    cg_A.array()
                        * (RefScale::F_atm * AX.array().abs() * AX.array() + // atm forcing
                            RefScale::F_ocean * (OX - vx).array().abs()
                                * OX.array()) // ocean forcing
                    + RefScale::rho_ice * cg_H.array() * RefScale::fc
                        * (vx - OX).array() // cor + surface
                    ))
                     .matrix();
            vy = (1.0
                / (RefScale::rho_ice * cg_H.array() / dt_momentum // implicit parts
                    + cg_A.array() * RefScale::F_ocean
                        * (OY.array() - vy.array()).abs()) // implicit parts
                * (RefScale::rho_ice * cg_H.array() / dt_momentum * vy.array()
                    + cg_A.array()
                        * (RefScale::F_atm * AY.array().abs() * AY.array() + // atm forcing
                            RefScale::F_ocean * (OY - vy).array().abs()
                                * OY.array()) // ocean forcing
                    + RefScale::rho_ice * cg_H.array() * RefScale::fc
                        * (OY - vy).array() // cor + surface
                    ))
                     .matrix();
            Nextsim::GlobalTimer.stop("time loop - meb - update1");

            Nextsim::GlobalTimer.start("time loop - meb - update2");

            // Implicit etwas ineffizient
            tmpx.zero();
            tmpy.zero();
            momentum.AddStressTensor(mesh, -1.0, tmpx, tmpy, S11, S12, S22);

            vx += (1.0
                / (RefScale::rho_ice * cg_H.array() / dt_momentum // implicit parts
                    + cg_A.array() * RefScale::F_ocean
                        * (OX.array() - vx.array()).abs()) // implicit parts
                * tmpx.array())
                      .matrix();
            vy += (1.0
                / (RefScale::rho_ice * cg_H.array() / dt_momentum // implicit parts
                    + cg_A.array() * RefScale::F_ocean
                        * (OY.array() - vy.array()).abs()) // implicit parts
                * tmpy.array())
                      .matrix();

            Nextsim::GlobalTimer.stop("time loop - meb - update2");
            Nextsim::GlobalTimer.stop("time loop - meb - update");

            Nextsim::GlobalTimer.start("time loop - meb - bound.");
            momentum.DirichletZero(mesh, vx);
            momentum.DirichletZero(mesh, vy);
            Nextsim::GlobalTimer.stop("time loop - meb - bound.");
        }
        Nextsim::GlobalTimer.stop("time loop - meb");

        //! Output
        if (WRITE_VTK)
            if ((timestep % NT_vtk == 0)) {
                std::cout << "VTK output at day " << time / 24. / 60. / 60. << std::endl;

                size_t printstep = timestep / NT_vtk + 1.e-4;

                char s[80];
                sprintf(s, "ResultsMEB_CGvel/ellipse_%03ld.txt", printstep);
                std::ofstream ELLOUT(s);
                for (size_t i = 0; i < mesh.n; ++i)
                    ELLOUT << S1(i, 0) << "\t" << S2(i, 0) << std::endl;
                ELLOUT.close();

                Nextsim::GlobalTimer.start("time loop - i/o");
                Nextsim::VTK::write_cg("ResultsMEB_CGvel/vx", printstep, vx, mesh);
                Nextsim::VTK::write_cg("ResultsMEB_CGvel/vy", printstep, vy, mesh);

                Nextsim::Tools::Delta(mesh, E11, E12, E22, RefScale::DeltaMin, DELTA);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/Delta", printstep, DELTA, mesh);
                Nextsim::Tools::Shear(mesh, E11, E12, E22, RefScale::DeltaMin, SHEAR);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/Shear", printstep, SHEAR, mesh);

                Nextsim::VTK::write_dg("ResultsMEB_CGvel/A", printstep, A, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/H", printstep, H, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/D", printstep, D, mesh);

                Nextsim::VTK::write_dg("ResultsMEB_CGvel/S11", printstep, S11, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/S12", printstep, S12, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/S22", printstep, S22, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/E11", printstep, E11, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/E12", printstep, E12, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/E22", printstep, E22, mesh);

                Nextsim::Tools::ElastoParams(
                    mesh, E11, E12, E22, H, A, RefScale::DeltaMin, RefScale::Pstar, MU1, MU2);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/mu1", printstep, MU1, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/mu2", printstep, MU2, mesh);

                Nextsim::VTK::write_dg(
                    "ResultsMEB_CGvel/stressOutside", printstep, sigma_outside, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/tildeP", printstep, tildeP, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/Pmax", printstep, Pmax, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/td", printstep, td, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/d_crit", printstep, d_crit, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/sigma_n", printstep, S1, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/tau", printstep, S2, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/eta1", printstep, eta1, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/eta2", printstep, eta2, mesh);
                Nextsim::VTK::write_dg(
                    "ResultsMEB_CGvel/stressrelax", printstep, stressrelax, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/Regime", printstep, Regime, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/Multip", printstep, Multip, mesh);
                Nextsim::VTK::write_dg("ResultsMEB_CGvel/Lambda", printstep, Lambda, mesh);

                Nextsim::GlobalTimer.stop("time loop - i/o");
            }
    }
    Nextsim::GlobalTimer.stop("time loop");

    std::cout << std::endl;
    Nextsim::GlobalTimer.print();
}