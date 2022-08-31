/*!
 * @file MEBSandbox.hpp
 * @date 1 Jun 2022
 * @author Piotr Minakowski <piotr.minakowski@ovgu.de>
 */

#ifndef __MEBSandbox_HPP
#define __MEBSandbox_HPP

#include "MEB.hpp"
#include "codeGenerationDGinGauss.hpp"
#include "dgVector.hpp"

namespace Nextsim {

/*!
 * This namespace collects the routines required for the MEB solver
 */
namespace MEBSandbox {

    inline constexpr double SQR(double x) { return x * x; }

    /**
     * @brief Calculate Stresses for the current time step and update damage.
     *
     * @details MEB model with additional outputs for development
     *
     * @tparam DGstress Stress adn Strain DG degree
     * @tparam DGtracer H, A, D DG degree
     * @param mesh mesh
     * @param S11 Stress component 11
     * @param S12 Stress component 12
     * @param S22 Stress component 22
     * @param E11 Strain component 11
     * @param E12 Strain component 12
     * @param E22 Strain component 22
     * @param H ice height
     * @param A ice concentation
     * @param D damage
     * @param DELTA delta
     * @param SHEAR shear
     * @param S1 Stress invariant 1
     * @param S2 Stress invariant 2
     * @param eta1 viscosity 1
     * @param eta2 viscosity 2
     * @param stressrelax indicator where stress is relaxed
     * @param sigma_outside indicator where sigma is outside plastic envelope
     * @param tP tilda P
     * @param Pm maximum P
     * @param dt_momentum timestep for momentum subcycle
     */
    template <int DGstress, int DGtracer>
    void StressUpdateSandbox(const Mesh& mesh, CellVector<DGstress>& S11, CellVector<DGstress>& S12,
        CellVector<DGstress>& S22, const CellVector<DGstress>& E11, const CellVector<DGstress>& E12,
        const CellVector<DGstress>& E22, const CellVector<DGtracer>& H,
        const CellVector<DGtracer>& A, CellVector<DGtracer>& D, CellVector<DGtracer>& DELTA,
        CellVector<DGtracer>& SHEAR, CellVector<DGtracer>& S1, CellVector<DGtracer>& S2,
        CellVector<DGtracer>& eta1, CellVector<DGtracer>& eta2, CellVector<DGtracer>& stressrelax,
        CellVector<DGtracer>& sigma_outside, CellVector<DGtracer>& tP, CellVector<DGtracer>& Pm,
        CellVector<DGtracer>& Td, CellVector<DGtracer>& d_crit, CellVector<DGtracer>& Regime,
        CellVector<DGtracer>& Multip, CellVector<DGtracer>& Lambda,

        const double dt_momentum)
    {
#pragma omp parallel for
        for (size_t i = 0; i < mesh.n; ++i) {

            // Check 1: Not mentioned in the paper
            // There's no ice so we set sigma to 0 and carry on
            const double min_c = 0.1;
            // if (M_conc[cpt] <= min_c) {
            if (A(i, 0) <= min_c) {

                // M_damage[cpt] = 0.;
                D(i, 0) = 0.0;
                // for(int i=0;i<3;i++)
                //     M_sigma[i][cpt] = 0.;
                S11.row(i) *= 0.0;
                S12.row(i) *= 0.0;
                S22.row(i) *= 0.0;
                std::cout << "NO ICE" << std::endl;
                continue;
            }

            //======================================================================
            //! - Updates the internal stress
            //======================================================================

            // Compute Pmax Eqn.(8) the way like in nextsim finiteelement.cpp
            double sigma_n = 0.5 * (S11(i, 0) + S22(i, 0));
            // shear stress
            double tau = std::hypot(0.5 * (S11(i, 0) - S22(i, 0)), S12(i, 0));
            assert(tau >= 0);

            const double C_fix = RefScale::C_lab * std::sqrt(0.1 / mesh.hx);
            // std::cout << "cfix = " << C_fix << std::endl; std::abort();

            // Outside Envelope
            sigma_outside(i) = 0;
            if (C_fix + RefScale::tan_phi * sigma_n <= tau)
                sigma_outside(i) = tau / (C_fix + RefScale::tan_phi * sigma_n);

            double const expC = std::exp(RefScale::compaction_param * (1. - A(i, 0)));

            double const time_viscous = RefScale::undamaged_time_relaxation_sigma
                * std::pow((1. - D(i, 0)) * expC, RefScale::exponent_relaxation_sigma - 1.);

            // Plastic failure tildeP
            double tildeP;
            double const Pmax
                = RefScale::compression_factor * pow(H(i, 0), 1.5) * exp(-20.0 * (1.0 - A(i, 0)));

            // Strange Function make plot
            if (sigma_n < 0.) {
                tildeP = std::min(1., -Pmax / sigma_n);
            } else {
                tildeP = 0.;
            }

            // Region
            if (sigma_n > 0)
                Regime(i) = 3;
            else if (-Pmax > sigma_n)
                Regime(i) = 2;
            else
                Regime(i) = 1;

            // tildeP = 0.0;
            tP(i) = tildeP;
            Pm(i) = Pmax;
            // \lambda / (\lambda + dt*(1.+tildeP)) Eqn. 32
            double const multiplicator
                = std::min(1. - 1e-12, time_viscous / (time_viscous + dt_momentum * (1. - tildeP)));

            Multip(i) = multiplicator;
            Lambda(i) = time_viscous;

            double const elasticity = RefScale::young * (1. - D(i, 0)) * expC;
            double const Dunit_factor = 1. / (1. - (RefScale::nu0 * RefScale::nu0));

            // Elasit prediction Eqn. (32)
            S11.row(i) += dt_momentum * elasticity
                * (1. / (1. + RefScale::nu0) * E11.row(i)
                    + Dunit_factor * RefScale::nu0 * (E11.row(i) + E22.row(i)));
            S12.row(i) += dt_momentum * elasticity * 1. / (1. + RefScale::nu0) * E12.row(i);
            S22.row(i) += dt_momentum * elasticity
                * (1. / (1. + RefScale::nu0) * E22.row(i)
                    + Dunit_factor * RefScale::nu0 * (E11.row(i) + E22.row(i)));

            eta1(i) = elasticity * (1. / (1. + RefScale::nu0));
            eta2(i) = elasticity * Dunit_factor * RefScale::nu0;

            // Eqn. (34)
            S11.row(i) *= multiplicator;
            S12.row(i) *= multiplicator;
            S22.row(i) *= multiplicator;

            //======================================================================
            //! - Estimates the level of damage from the updated internal stress and the local
            //! damage criterion
            //======================================================================

            // continiue if stress in inside the failure envelope
            //  Compute the shear and normal stresses, which are two invariants of the internal
            //  stress tensor
            double const sigma_s = std::hypot((S11(i, 0) - S22(i, 0)) / 2., S12(i, 0));
            // update sigma_n
            sigma_n = 0.5 * (S11(i, 0) + S22(i, 0));

            // Check 3: Discuss Cohesion with Einar
            // Cohesion Eqn. (21)
            // Reference length scale is fixed 0.1 since its cohesion parameter at the lab scale (10
            // cm) C_lab;...  : cohesion (Pa)

            // d critical Eqn. (29)

            double dcrit;
            if (sigma_n < -RefScale::compr_strength)
                dcrit = -RefScale::compr_strength / sigma_n;
            else
                // M_Cohesion[cpt] depends on local random contribution
                // M_Cohesion[i] = C_fix+C_alea*(M_random_number[i]);
                // finiteelement.cpp#L3834
                dcrit = C_fix / (sigma_s + RefScale::tan_phi * sigma_n);

            // check minus below
            // dcrit = C_fix / (sigma_s + RefScale::tan_phi * sigma_n);

            // Calculate the characteristic time for damage and damage increment
            // M_delta_x[cpt] = mesh.hx ???
            double td = mesh.hx * std::sqrt(2. * (1. + RefScale::nu0) * RefScale::rho_ice)
                / std::sqrt(elasticity);

            Td(i) = td;
            td = 20;
            d_crit(i) = dcrit;
            // Calculate the adjusted level of damage
            stressrelax(i) = 0.0;

            // change that if
            if ((0. < dcrit)
                && (dcrit
                    < 1.)) // sigma_s - tan_phi*sigma_n < 0 is always inside, but gives dcrit < 0
            // if (C_fix + RefScale::tan_phi * sigma_n <= sigma_s )
            {
                // std::cout << "DAMAGE" << std::endl;
                // SAVE DAMAGE
                stressrelax(i) = 1.0;

                // Eqn. (34)
                D(i, 0) += (1.0 - D(i, 0)) * (1.0 - dcrit) * dt_momentum / td;

                // Recalculate the new state of stress by relaxing elstically Eqn. (36)
                S11.row(i) -= S11.row(i) * (1. - dcrit) * dt_momentum / td;
                S12.row(i) -= S12.row(i) * (1. - dcrit) * dt_momentum / td;
                S22.row(i) -= S22.row(i) * (1. - dcrit) * dt_momentum / td;
            }

            // Relax damage
            // Check 4: Values are not in the Paper, cf. Eqn. (30)
            // D(i, 0) = std::max(0., D(i, 0) - dt_momentum / RefScale::time_relaxation_damage *
            // std::exp(-20. * (1. - A(i, 0))));
            D(i, 0) = std::max(0., D(i, 0) - dt_momentum / RefScale::time_relaxation_damage);

            // ellipse-output
            S1(i) = 0.5 * (S11(i, 0) + S22(i, 0));
            S2(i) = std::hypot(S12(i, 0), 0.5 * (S11(i, 0) - S22(i, 0)));
        }
    }
    /*!
     * @brief Stress update for VP Model
     *
     * @details Function used to validate frocing gives same results as mEVP
     *
     *
     * @tparam DGstress Stress adn Strain DG degree
     * @tparam DGtracer H, A, D DG degree
     * @param mesh mesh
     * @param S11 Stress component 11
     * @param S12 Stress component 12
     * @param S22 Stress component 22
     * @param E11 Strain component 11
     * @param E12 Strain component 12
     * @param E22 Strain component 22
     * @param H ice height
     * @param A ice concentation
     * @param Pstar ice strength
     * @param DeltaMin Delta min 2e-9
     * @param dt_momentum timestep for momentum subcycle
     */
    template <int DGstress, int DGtracer>
    void StressUpdateVP(const Mesh& mesh, CellVector<DGstress>& S11, CellVector<DGstress>& S12,
        CellVector<DGstress>& S22, const CellVector<DGstress>& E11, const CellVector<DGstress>& E12,
        const CellVector<DGstress>& E22, const CellVector<DGtracer>& H,
        const CellVector<DGtracer>& A, const double Pstar, const double DeltaMin,
        const double dt_momentum)
    {

        //! Stress Update
#pragma omp parallel for
        for (size_t i = 0; i < mesh.n; ++i) {
            double DELTA = sqrt(SQR(DeltaMin) + 1.25 * (SQR(E11(i, 0)) + SQR(E22(i, 0)))
                + 1.50 * E11(i, 0) * E22(i, 0) + SQR(E12(i, 0)));

            assert(DELTA > 0);

            //! Ice strength
            double P = Pstar * H(i, 0) * exp(-20.0 * (1.0 - A(i, 0)));

            double zeta = P / 2.0 / DELTA;
            double eta = zeta / 4;

            // VP
            S11.row(i) = (2. * eta * E11.row(i) + (zeta - eta) * (E11.row(i) + E22.row(i)));
            S11(i, 0) -= 0.5 * P;

            S12.row(i) = (2. * eta * E12.row(i));

            S22.row(i) = (2. * eta * E22.row(i) + (zeta - eta) * (E11.row(i) + E22.row(i)));
            S22(i, 0) -= 0.5 * P;
        }
    }

    template <int DGstress, int DGtracer>
    void ElasticUpdate(const Mesh& mesh, CellVector<DGstress>& S11, CellVector<DGstress>& S12,
        CellVector<DGstress>& S22, const CellVector<DGstress>& E11, const CellVector<DGstress>& E12,
        const CellVector<DGstress>& E22, const CellVector<DGtracer>& H,
        const CellVector<DGtracer>& A, CellVector<DGtracer>& D, const double dt_momentum)
    {

        //! Stress Update
#pragma omp parallel for
        for (size_t i = 0; i < mesh.n; ++i) {

            //! - Updates the internal stress
            double const expC = std::exp(RefScale::compaction_param * (1. - A(i, 0)));

            double const elasticity = RefScale::young * (1. - D(i, 0)) * expC;
            double const Dunit_factor = 1. / (1. - (RefScale::nu0 * RefScale::nu0));

            // Elasit prediction Eqn. (32)
            S11.row(i) = RefScale::young
                * (1 / (1 + RefScale::nu0) * E11.row(i)
                    + Dunit_factor * RefScale::nu0 * (E11.row(i) + E22.row(i)));
            S12.row(i) = RefScale::young * 1 / (1 + RefScale::nu0) * E12.row(i);
            S22.row(i) = RefScale::young
                * (1 / (1 + RefScale::nu0) * E22.row(i)
                    + Dunit_factor * RefScale::nu0 * (E11.row(i) + E22.row(i)));
        }
    }

    template <int DGstress, int DGtracer>
    void ViscoElasticUpdate(const Mesh& mesh, CellVector<DGstress>& S11, CellVector<DGstress>& S12,
        CellVector<DGstress>& S22, const CellVector<DGstress>& E11, const CellVector<DGstress>& E12,
        const CellVector<DGstress>& E22, const CellVector<DGtracer>& H,
        const CellVector<DGtracer>& A, CellVector<DGtracer>& D, const double dt_momentum)
    {

        //! Stress Update
#pragma omp parallel for
        for (size_t i = 0; i < mesh.n; ++i) {

            double const expC = std::exp(RefScaleCanada::compaction_param * (1. - A(i, 0)));

            // Eqn. 25
            double time_viscous = RefScaleCanada::undamaged_time_relaxation_sigma
                * std::pow((1. - D(i, 0)), RefScaleCanada::exponent_relaxation_sigma - 1.) / (H(i, 0) * expC);

            // Eqn. 24 Additional multiplic0cation by H
            double elasticity = RefScaleCanada::young * H(i, 0) * (1. - D(i, 0)) * expC;

            // 1. / (1. + dt / lambda) Eqn. 18
            double const multiplicator = 1. / (1. + dt_momentum / time_viscous);

            // time_viscous = RefScaleCanada::undamaged_time_relaxation_sigma;
            // elasticity = RefScaleCanada::young;

            double const Dunit_factor = 1. / (1. - (RefScale::nu0 * RefScale::nu0));

            // Elasit prediction Eqn. (32)
            S11.row(i) += dt_momentum * elasticity
                * (1. / (1. + RefScaleCanada::nu0) * E11.row(i)
                    + Dunit_factor * RefScaleCanada::nu0 * (E11.row(i) + E22.row(i)));
            S12.row(i) += dt_momentum * elasticity * 1. / (1. + RefScaleCanada::nu0) * E12.row(i);
            S22.row(i) += dt_momentum * elasticity
                * (1. / (1. + RefScaleCanada::nu0) * E22.row(i)
                    + Dunit_factor * RefScaleCanada::nu0 * (E11.row(i) + E22.row(i)));

            // Eqn. (34)
            S12.row(i) *= multiplicator;
            S11.row(i) *= multiplicator;
            S22.row(i) *= multiplicator;
        }
    }

    //! CG1-DG1
    void ViscoElasticUpdate(const Mesh& mesh, CellVector<3>& S11, CellVector<3>& S12,
        CellVector<3>& S22, const CellVector<3>& E11, const CellVector<3>& E12,
        const CellVector<3>& E22, const CellVector<3>& H,
        const CellVector<3>& A, CellVector<3>& D, const double dt_momentum)
    {

        //! Stress Update
#pragma omp parallel for
        for (size_t i = 0; i < mesh.n; ++i) {

            //!  number of gauss points in each direction equalls 3, for 2d 3^2
            const Eigen::Matrix<double, 1, 9> h_gauss = (H.block<1, 3>(i, 0) * PSI33).array().max(0.0).matrix();
            const Eigen::Matrix<double, 1, 9> a_gauss = (A.block<1, 3>(i, 0) * PSI33).array().max(0.0).min(1.0).matrix();
            const Eigen::Matrix<double, 1, 9> d_gauss = (D.block<1, 3>(i, 0) * PSI33).array().max(0.0).min(1.0).matrix();

            const Eigen::Matrix<double, 1, 9> e11_gauss = E11.block<1, 3>(i, 0) * PSI33;
            const Eigen::Matrix<double, 1, 9> e12_gauss = E12.block<1, 3>(i, 0) * PSI33;
            const Eigen::Matrix<double, 1, 9> e22_gauss = E22.block<1, 3>(i, 0) * PSI33;

            auto expC = (-20.0 * (1.0 - a_gauss.array())).exp();
            // Eqn. 24 Additional multiplic0cation by H
            const Eigen::Matrix<double, 1, 9> elasticity = (RefScaleCanada::young * h_gauss.array() * (1. - d_gauss.array()) * expC.array()).matrix();

            auto powalpha = (1. - d_gauss.array()).pow(RefScaleCanada::exponent_relaxation_sigma - 1.);
            // Eqn. 25
            const Eigen::Matrix<double, 1, 9> time_viscous = (RefScaleCanada::undamaged_time_relaxation_sigma * powalpha.array()).matrix();

            double const Dunit_factor = 1. / (1. - (RefScale::nu0 * RefScale::nu0));

            // 1. / (1. + dt / lambda) Eqn. 18
            const Eigen::Matrix<double, 1, 3> multiplicator = (1. / (1. + dt_momentum / time_viscous.array())).matrix() * IBC33;

            // Elasit prediction Eqn. (32)
            S11.row(i) += dt_momentum * 1. / (1. + RefScaleCanada::nu0) * (elasticity.array() * e11_gauss.array()).matrix() * IBC33
                + dt_momentum * Dunit_factor * RefScaleCanada::nu0 * (elasticity.array() * (e11_gauss.array() + e22_gauss.array())).matrix() * IBC33;

            S12.row(i) += dt_momentum * 1. / (1. + RefScaleCanada::nu0) * (elasticity.array() * e12_gauss.array()).matrix() * IBC33;

            S22.row(i) += dt_momentum * 1. / (1. + RefScaleCanada::nu0) * (elasticity.array() * e22_gauss.array()).matrix() * IBC33
                + dt_momentum * Dunit_factor * RefScaleCanada::nu0 * (elasticity.array() * (e11_gauss.array() + e22_gauss.array())).matrix() * IBC33;

            // Eqn. (34)
            //! Coefficient-wise multiplication
            S12.row(i).array() *= multiplicator.array();
            S11.row(i).array() *= multiplicator.array();
            S22.row(i).array() *= multiplicator.array();

            // S12.row(i).array() *= 1. / (1. + dt_momentum / RefScaleCanada::undamaged_time_relaxation_sigma);
            // S11.row(i).array() *= 1. / (1. + dt_momentum / RefScaleCanada::undamaged_time_relaxation_sigma);
            // S22.row(i).array() *= 1. / (1. + dt_momentum / RefScaleCanada::undamaged_time_relaxation_sigma);
        }
    }

} /* namespace MEBSandbox */

} /* namespace Nextsim */

#endif /* __MEBSANDBOX_HPP */