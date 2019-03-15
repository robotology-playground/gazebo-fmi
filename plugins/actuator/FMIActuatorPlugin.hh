/*
 * Copyright (C) 2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
/*
 * Copyright (C) 2018 Fondazione Istituto Italiano di Tecnologia
 *
 * Licensed under either the GNU Lesser General Public License v3.0 :
 * https://www.gnu.org/licenses/lgpl-3.0.html
 * or the GNU Lesser General Public License v2.1 :
 * https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
 * at your option.
 */


#ifndef GAZEBO_FMI_ACTUATORPLUGIN_
#define GAZEBO_FMI_ACTUATORPLUGIN_

#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <gazebo/common/Events.hh>
#include <gazebo/physics/physics.hh>
#include <gazebo/gazebo.hh>

#include <gazebo_fmi/SDFConfigurationParsing.hh>
#include <gazebo_fmi/FMUCoSimulation.hh>

/// Example SDF:
///       <plugin name="fmi_actuator_plugin" filename="libFMIActuatorPlugin.so">
///        <actuator>
///          <name>actuator_0</name> <!-- optional -->
///          <joint>JOINT_0</joint> <!-- name of joint to actuate in the model -->
///          <fmu>electric_motor</fmu> <!-- name of the fmu -->
///        </actuator>
///       </plugin>
///    </model>
///
/// Required fields:
/// - name
/// - joint
/// - fmu:
/// Optional fields:
/// - variable_names


// INDICES: INPUTS, OUTPUT
namespace FMIActuatorPluginNS
{
enum InputIndex
{
    actuatorInput = 0,
    jointPosition,
    jointVelocity,
    jointAcceleration,
    TotalInputs,
};

enum OutputIndex
{
    jointTorque = 0,
    TotalOutputs,
};
}

namespace gazebo_fmi
{
    /// \brief Properties for a model of a rotational actuator
    class FMUActuatorProperties
    {
        public: FMUActuatorProperties()
        {
            // Configure default variable names
            m_inputVariablesDefaultNames.resize(FMIActuatorPluginNS::TotalInputs);
            m_inputVariablesDefaultNames[FMIActuatorPluginNS::actuatorInput]     = "actuatorInput";
            m_inputVariablesDefaultNames[FMIActuatorPluginNS::jointPosition]     = "jointPosition";
            m_inputVariablesDefaultNames[FMIActuatorPluginNS::jointVelocity]     = "jointVelocity";
            m_inputVariablesDefaultNames[FMIActuatorPluginNS::jointAcceleration] = "jointAcceleration";

            m_outputVariablesDefaultNames.resize(FMIActuatorPluginNS::TotalOutputs);
            m_outputVariablesDefaultNames[FMIActuatorPluginNS::jointTorque]     = "jointTorque";
        }

        /// \brief An identifier for the actuator.
        public: std::string m_name;

        public: std::string m_fmuAbsolutePath;

        /// \brief Default input variable names
        public: std::vector<std::string> m_inputVariablesDefaultNames;

        /// \brief Actual input variable names, after parsing variable_names
        public: std::vector<std::string> m_inputVariablesNames;

        /// \brief Default output variable names
        public: std::vector<std::string> m_outputVariablesDefaultNames;

        /// \brief Actual output variable names, after parsing variable_names
        public: std::vector<std::string> m_outputVariablesNames;

        /// \brief Flag to indicate that Gazebo's velocit and effort limits should be disabled
        public: bool disableVelocityEffortLimits{false};

        public: FMUCoSimulation m_fmu;
        public: std::vector<fmi2_value_reference_t> m_inputVarReferences;
        public: std::vector<fmi2_value_reference_t> m_outputVarReferences;
        public: std::vector<double> m_inputVarBuffers;
        public: std::vector<double> m_outputVarBuffers;

        /// \brief Flag to indicate that the plugin is enabled
        public: bool m_enabled{true};

        /// \brief The joints we want to actuate
        public: gazebo::physics::JointPtr m_joint;

    };

    using FMUActuatorProperties_sptr=std::shared_ptr<FMUActuatorProperties>;

    /// \brief Plugin for simulating a torque-speed curve for actuators.
    class GAZEBO_VISIBLE FMIActuatorPlugin : public gazebo::ModelPlugin
    {
        /// Documentation inherited
        public: void Load(gazebo::physics::ModelPtr _parent, sdf::ElementPtr _sdf);

        private: bool ParseParameters(gazebo::physics::ModelPtr _parent, sdf::ElementPtr _sdf);

        private: bool LoadFMUs(gazebo::physics::ModelPtr _parent);
        
        private: gazebo::physics::JointPtr FindJointInModel(const std::string& jointName,const gazebo::physics::ModelPtr _parent);

        /// \brief Sanity check on the joint
        private: bool CheckJointType(gazebo::physics::JointPtr jointPtr);

        /// \brief Disable the velocity and effort limits (if the specific option is enabled)
        private: bool DisableVelocityEffortLimits();

        /// \brief Compute joint acceleration (not directly provided by Gazebo)
        private: double GetJointAcceleration(gazebo::physics::JointPtr jointPtr);

        /// \brief Callback on before physics update event
        private: void BeforePhysicsUpdateCallback(const gazebo::common::UpdateInfo & updateInfo);

        /// \brief Corresponding actuator properties (power, max torque, etc.)
        private: std::vector<FMUActuatorProperties_sptr> m_actuators;

        /// \brief Connections to events associated with this class.
        private: std::vector<gazebo::event::ConnectionPtr> m_connections;

        /// \brief False for simbody, true for ode|bullet|dart, see https://bitbucket.org/osrf/gazebo/issues/2507/joint-setforce-is-not-additive-in-simbody
        private: bool m_isSetForceCumulative{true};

        /// \brief Flag to indicate that we want the plugin to log in a verbose way
        private: bool m_verbose{false};
    };

    // Register this plugin with the simulator
    GZ_REGISTER_MODEL_PLUGIN(FMIActuatorPlugin);
}

#endif
