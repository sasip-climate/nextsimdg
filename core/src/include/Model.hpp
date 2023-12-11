/*!
 * @file Model.hpp
 * @date 12 Aug 2021
 * @author Tim Spain <timothy.spain@nersc.no>
 * @author Kacper Kornet <kk562@cam.ac.uk>
 */

#ifndef MODEL_HPP
#define MODEL_HPP

#include "include/Logged.hpp"

#include "include/Configured.hpp"
#include "include/Iterator.hpp"
#include "include/ModelMetadata.hpp"
#include "include/ModelState.hpp"
#include "include/PrognosticData.hpp"

#include "DevStep.hpp"
#include <string>

namespace Nextsim {

//! A class that encapsulates the whole of the model
class Model : public Configured<Model> {
public:
#ifdef USE_MPI
    Model(MPI_Comm comm);
#else
    Model(); // TODO add arguments to pass the desired
             // environment and configuration to the model
#endif
    ~Model(); // Finalize the model. Collect data and so on.

    void configure() override;
    enum {
        RESTARTFILE_KEY,
#ifdef USE_MPI
        PARTITIONFILE_KEY,
#endif
        STARTTIME_KEY,
        STOPTIME_KEY,
        RUNLENGTH_KEY,
        TIMESTEP_KEY,
        MISSINGVALUE_KEY,
    };

    ConfigMap getConfig() const;

    static HelpMap& getHelpText(HelpMap& map, bool getAll);
    static HelpMap& getHelpRecursive(HelpMap& map, bool getAll);

    //! Run the model
    void run();

    void writeRestartFile();

    //! Sets the filename of the restart file that would currently be written out.
    void setFinalFilename(const std::string& finalFile);

    //! Gets the model metadata instance
    ModelMetadata& metadata();

    // Configuration option that holds the restart file name
    const static std::string restartOptionName;

private:
    Iterator iterator;
    DevStep modelStep; // Change the model step calculation here
    PrognosticData pData;
    ModelMetadata m_etadata;

    std::string initialFileName;
    std::string finalFileName;

    // Cached values of the start-step-stop/duration times
    std::string startTimeStr;
    std::string stopTimeStr;
    std::string durationStr;
    std::string stepStr;
};

} /* namespace Nextsim */

#endif /* MODEL_HPP */
