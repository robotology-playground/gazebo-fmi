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


#include "FMIActuatorPlugin.hh"

#include <functional>

#include <experimental/filesystem>

#include <gazebo_fmi/SDFConfigurationParsing.hh>


using namespace gazebo_fmi;

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


//////////////////////////////////////////////////
void FMIActuatorPlugin::Load(gazebo::physics::ModelPtr _parent, sdf::ElementPtr _sdf)
{
    // Configure default variable names
    m_actuator.m_inputVariablesDefaultNames.resize(FMIActuatorPluginNS::TotalInputs);
    m_actuator.m_inputVariablesDefaultNames[FMIActuatorPluginNS::actuatorInput]     = "actuatorInput";
    m_actuator.m_inputVariablesDefaultNames[FMIActuatorPluginNS::jointPosition]     = "jointPosition";
    m_actuator.m_inputVariablesDefaultNames[FMIActuatorPluginNS::jointVelocity]     = "jointVelocity";
    m_actuator.m_inputVariablesDefaultNames[FMIActuatorPluginNS::jointAcceleration] = "jointAcceleration";

    m_actuator.m_outputVariablesDefaultNames.resize(FMIActuatorPluginNS::TotalOutputs);
    m_actuator.m_outputVariablesDefaultNames[FMIActuatorPluginNS::jointTorque]     = "jointTorque";


    // Handle Gazebo Simbody (  https://bitbucket.org/osrf/gazebo/issues/2507/joint-setforce-is-not-additive-in-simbody )
    // and DART ( https://bitbucket.org/osrf/gazebo/issues/2526/joint-setforce-is-not-additive-in-dart-in ) bug
    if (gazebo::physics::get_world()->Physics()->GetType() == "simbody" ||
        gazebo::physics::get_world()->Physics()->GetType() == "dart")
    {
        this->m_isSetForceCumulative = false;
    }

    // Parse parameters
    if (!this->ParseParameters(_parent, _sdf))
    {
        gzerr << "FMIActuatorPlugin: error in parsing SDF parameters, plugin loading failed."
              << std::endl;
    }

    if(!m_actuator.m_enabled)
    {
        gzwarn << "FMIActuatorPlugin: actuator disabled: "<<m_actuator.m_name
              << std::endl;
        return;
    }

    // If necessary disable joint limits
    this->DisableVelocityEffortLimits();

    // Try to open fmu for all joints in the plugin
    if (!this->LoadFMUs(_parent))
    {
        gzerr << "FMIActuatorPlugin: error in loading FMUs, plugin loading failed."
              << std::endl;
        return;
    }

    // Set up a physics update callback
    this->m_connections.push_back(gazebo::event::Events::ConnectBeforePhysicsUpdate(
      boost::bind(&FMIActuatorPlugin::BeforePhysicsUpdateCallback, this, _1)));
}

//////////////////////////////////////////////////
// Read the SDF
bool FMIActuatorPlugin::ParseParameters(gazebo::physics::ModelPtr _parent, sdf::ElementPtr _sdf)
{
  if (_sdf->HasElement("verbose"))
  {
      m_verbose = _sdf->Get<bool>("verbose");
  }

  if (_sdf->HasElement("actuator"))
  {
    // TODO(traversaro) : support list of actuator elements
    sdf::ElementPtr elem = _sdf->GetElement("actuator");
    {

      if (elem->HasElement("enabled"))
      {
        m_actuator.m_enabled = elem->Get<bool>("enabled");               
      }
        

      // actuator name is currently an optional property
      if (elem->HasElement("name"))
        m_actuator.m_name = elem->Get<std::string>("name");

      if (!elem->HasElement("joint"))
      {
         gzerr << "FMIActuatorPlugin: Invalid SDF, got actuator element without joint."
               << std::endl;
        return false;
      }
      std::string jointName = elem->Get<std::string>("joint");

      m_joint=FindJointInModel(jointName,_parent);

      // Store pointer to the joint we will actuate
      if (!m_joint)
      {
         gzerr << "FMIActuatorPlugin: Invalid SDF, actuator joint " << jointName << " does not "
               << "exist!" << std::endl;
        return false;
      }

      // Check type
      bool typeOk = this->CheckJointType(m_joint);
      if (!typeOk)
      {
        return false;
      }


      if (!elem->HasElement("fmu"))
      {
         gzerr << "FMIActuatorPlugin: Invalid SDF, fmu parameter not found."
               << std::endl;
        return false;
      }

      m_actuator.m_fmuAbsolutePath =  gazebo::common::SystemPaths::Instance()->FindFile(elem->Get<std::string>("fmu"));
      if (!std::experimental::filesystem::exists(m_actuator.m_fmuAbsolutePath))
      {
        gzerr << "FMIActuatorPlugin: Impossible to find FMU named " << elem->Get<std::string>("fmu")
              << " in the GAZEBO_RESOURCE_PATH directories" << std::endl;
        return false;
      }


      if (elem->HasElement("disable_velocity_effort_limits"))
      {
          m_actuator.disableVelocityEffortLimits = elem->Get<bool>("disable_velocity_effort_limits");
      }
      else
      {
          m_actuator.disableVelocityEffortLimits = false;
      }

      // Process variable_names element
      bool variableNamesOk = gazebo_fmi::parseVariableNamesSDFElement(elem,
                                                                      m_actuator.m_inputVariablesDefaultNames,
                                                                      m_actuator.m_inputVariablesNames,
                                                                      m_actuator.m_outputVariablesDefaultNames,
                                                                      m_actuator.m_outputVariablesNames);

      if (!variableNamesOk)
      {
        gzerr << "FMIActuatorPlugin: failure in parsing variable_names tag" << std::endl;
        return false;
      }

      // Legacy tags, used only for backcompatibiltiy
      if (elem->HasElement("actuatorInputName"))
      {
        gzwarn << "FMIActuatorPlugin: option actuatorInputName deprecated, please use variable_names tag instead." << std::endl;
        m_actuator.m_inputVariablesNames[FMIActuatorPluginNS::actuatorInput] = elem->Get<std::string>("actuatorInputName");
      }

      if (elem->HasElement("jointPositionName"))
      {
        gzwarn << "FMIActuatorPlugin: option jointPositionName deprecated, please use variable_names tag instead."  << std::endl;
        m_actuator.m_inputVariablesNames[FMIActuatorPluginNS::jointPosition] = elem->Get<std::string>("jointPositionName");
      }

      if (elem->HasElement("jointVelocityName"))
      {
        gzwarn << "FMIActuatorPlugin: option jointVelocityName deprecated, please use variable_names tag instead."  << std::endl;
        m_actuator.m_inputVariablesNames[FMIActuatorPluginNS::jointVelocity] = elem->Get<std::string>("jointVelocityName");
      }

      if (elem->HasElement("jointAccelerationName"))
      {
        gzwarn << "FMIActuatorPlugin: option jointAccelerationName deprecated, please use variable_names tag instead." << std::endl;
        m_actuator.m_inputVariablesNames[FMIActuatorPluginNS::jointAcceleration] = elem->Get<std::string>("jointAccelerationName");
      }

      if (elem->HasElement("jointTorqueName"))
      {
        gzwarn << "FMIActuatorPlugin: option jointTorqueName deprecated, please use variable_names tag instead." << std::endl;
        m_actuator.m_outputVariablesNames[FMIActuatorPluginNS::jointTorque] = elem->Get<std::string>("jointTorqueName");
      }
    
      if(m_verbose)
      {
        gzmsg << "FMIActuatorPlugin: actuator loaded for joint name:" << jointName << std::endl;
      }
    }


    return true;
  }
  else
  {
      gzerr << "FMIActuatorPlugin: Invalid SDF, no actuator tag found." << std::endl;
      return false;
  }
}

//////////////////////////////////////////////////
// Disable velocity and effort limits
bool FMIActuatorPlugin::DisableVelocityEffortLimits()
{
    //for (unsigned int i = 0; i < this->actuators.size(); i++)
    {
        if (m_actuator.disableVelocityEffortLimits)
        {
            m_joint->SetVelocityLimit(0u, -1.0);
            m_joint->SetEffortLimit(0u, -1.0);
        }
    }
}

//////////////////////////////////////////////////
bool FMIActuatorPlugin::LoadFMUs(gazebo::physics::ModelPtr _parent)
{
    double simulatedTimeInSeconds  = _parent->GetWorld()->SimTime().Double();


    //for (unsigned int i = 0; i < this->actuators.size(); i++)
    {
        // FMUActuatorProperties& actuator = this->actuators[i];
        // gazebo::physics::JointPtr joint
        std::string instanceName = m_joint->GetScopedName()+"_fmuTransmission";
        bool ok = m_actuator.m_fmu.load(m_actuator.m_fmuAbsolutePath, instanceName, simulatedTimeInSeconds);
        if (!ok) {
            return false;
        }

        // Get references for input variables
        ok = m_actuator.m_fmu.getInputVariableRefs(m_actuator.m_inputVariablesNames, m_actuator.m_inputVarReferences);
        if (!ok) {
            return false;
        }
        m_actuator.m_inputVarBuffers.resize(m_actuator.m_inputVarReferences.size());

        // Get references for output variables
        ok = m_actuator.m_fmu.getOutputVariableRefs(m_actuator.m_outputVariablesNames, m_actuator.m_outputVarReferences);
        if (!ok) {
            return false;
        }
        m_actuator.m_outputVarBuffers.resize(m_actuator.m_outputVarBuffers.size());
    }

    return true;
}

//////////////////////////////////////////////////
bool FMIActuatorPlugin::CheckJointType(gazebo::physics::JointPtr jointPtr)
{
    if (jointPtr->DOF() != 1)
    {
        gzerr << "FMIActuatorPlugin: joint " << jointPtr->GetScopedName() << " has "
              << jointPtr->DOF() << ", only joint with 1 DOFs are currently supported." << std::endl;
        return false;
    }

    if (!(jointPtr->GetType() & gazebo::physics::Base::HINGE_JOINT))
    {
        gzwarn << "FMIActuatorPlugin: joint " << jointPtr->GetScopedName() << " is not an HingeJoint. " << std::endl;
        gzwarn << "Feedback on joint acceleration is currently available only for HingeJoint, "
               << "so it is possible to use this joint only with an FMU that is not using acceleration feedback." << std::endl;
        gzwarn << "See https://github.com/robotology/gazebo-fmi/issues/17 for more details." << std::endl;
    }

    return true;
}

//////////////////////////////////////////////////
double FMIActuatorPlugin::GetJointAcceleration(gazebo::physics::JointPtr jointPtr)
{
    if (jointPtr->GetType() & gazebo::physics::Base::HINGE_JOINT)
    {
        // Compute joint acceleration
        // For a link `L`, the method ignition::math::Vector3d WorldAngularAccel () const
        // returns the angular acceleration \$ {}^A \omega_{A,L} \$.
        // Given a hinge/revolute joint ${P, C}$ connecting the links $P$ (parent) and $C$ (child), the method
        // virtual ignition::math::Vector3d GlobalAxis( unsigned int  _index) const
        // returns the angular part of the joint motion subspace \$  ^A s_{P,C} \in \mathbb{R}^6  \$
        // that we indicate with \$ {}^A a_{P, C} \in \mathbb{R}^3 \$.
        //
        // The joint acceleration \$\ddot{\theta} \in \mathbb{R}\$ can then be computed as:
        // \$
        // \ddot{\theta} =  \left( ^C s_{P,C} \right)^T  {}^C \dot{\omega}\_{P,C} =  \left( ^C s_{P,C} \right)^T  ( {}^C \dot{\omega}\_{A,C} - {}^C \dot{\omega}\_{A,P} )  =
        // \left( ^A s_{P,C} \right)^T ( {}^A \dot{\omega}\_{A,C} - {}^A \dot{\omega}\_{A,P} )
        // \$

        ignition::math::Vector3d A_axis_P_C = jointPtr->GlobalAxis(0u);
        gazebo::physics::LinkPtr parent = jointPtr->GetParent();
        gazebo::physics::LinkPtr child = jointPtr->GetChild();
        ignition::math::Vector3d A_domega_A_P = parent->WorldAngularAccel();
        ignition::math::Vector3d A_domega_A_C = child->WorldAngularAccel();
        return A_axis_P_C.Dot(A_domega_A_C-A_domega_A_P);
    }
    else
    {
        // For other joints, just return 0 as acceleration
        // Appropriate warning are printed during the load, and this ensures that
        // the models that do not use acceleration (such as compliant models) still work fine
        return 0.0;
    }
}


//////////////////////////////////////////////////
void FMIActuatorPlugin::BeforePhysicsUpdateCallback(const gazebo::common::UpdateInfo & updateInfo)
{
    // TODO(traversaro): review this part
    double simulatedTimeInSeconds  = updateInfo.simTime.Double();
    auto world = gazebo::physics::get_world(updateInfo.worldName);
    double stepSizeInSeconds = world->Physics()->GetMaxStepSize();


    // Update the stored joints according to the desired model.
    //for (unsigned int i = 0; i < this->joints.size(); i++)
    {
        //FMUActuatorProperties& actuator = this->actuators[i];

        double actuatorInput = m_joint->GetForce(0u);
        const double position = m_joint->Position(0u);
        const double velocity = m_joint->GetVelocity(0u);
        const double acceleration = this->GetJointAcceleration(m_joint);

        // This order should be coherent with the order defined in LoadFMUs
        m_actuator.m_inputVarBuffers[0] = actuatorInput;
        m_actuator.m_inputVarBuffers[1] = position;
        m_actuator.m_inputVarBuffers[2] = velocity;
        m_actuator.m_inputVarBuffers[3] = acceleration;

        // Set input
        bool ok = m_actuator.m_fmu.setInputVariables(m_actuator.m_inputVarReferences, m_actuator.m_inputVarBuffers);

        // Run fmu simulation
        ok = ok && m_actuator.m_fmu.doStep(simulatedTimeInSeconds, stepSizeInSeconds);

        // Get ouput
        ok = ok && m_actuator.m_fmu.getOutputVariables(m_actuator.m_outputVarReferences, m_actuator.m_outputVarBuffers);

        if (!ok)
        {
            gzerr << "gazebo_fmi: Failure in simulating trasmission of " << m_joint->GetScopedName() << std::endl;
        }

        // This order should be coherent with the order defined in LoadFMUs
        double jointTorque = m_actuator.m_outputVarBuffers[0];

        // Note: in ODE, Bullet and DART, two consecutive SetForce calls are added to the same buffer:
        // for this reason, to overwrite the previous value we subtract it from the desired value
        if (this->m_isSetForceCumulative)
        {
            m_joint->SetForce(0u, jointTorque-actuatorInput);
        }
        else
        {
            m_joint->SetForce(0u, jointTorque);
        }
    }
}

//////////////////////////////////////////////////
gazebo::physics::JointPtr FMIActuatorPlugin::FindJointInModel(const std::string& jointName,const gazebo::physics::ModelPtr _parent)
{
    m_joint=nullptr;
    for (auto currentJoint:_parent->GetJoints())
    {
        const std::string& currentJointName=currentJoint->GetName();

        if(currentJointName==jointName)//if you use the scoped name
        {
            m_joint=currentJoint;
            break;
        }

        std::size_t lastJointNamePart = currentJointName.find_last_of("::");
        std::string currentJointNameUnscoped;
        if (lastJointNamePart==std::string::npos)//in the model the joint names are unscoped
        {
            currentJointNameUnscoped=currentJointName;
        }
        else//in the model the joint names are scoped
        {
            currentJointNameUnscoped=currentJointName.substr(lastJointNamePart+1);
        }

        if (currentJointNameUnscoped!=jointName)
            continue;

        m_joint=currentJoint;
        break;
    } 
    return m_joint;
} 
