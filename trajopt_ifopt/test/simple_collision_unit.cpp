/**
 * @file simple_collision_unit.cpp
 * @brief A simple collision unit test for the discrete constraint
 *
 * @author Levi Armstrong
 * @author Matthew Powelson
 * @date May 18, 2020
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2020, Southwest Research Institute
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <trajopt_common/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <ctime>
#include <gtest/gtest.h>
#include <tesseract_environment/environment.h>
#include <tesseract_environment/utils.h>
#include <tesseract_visualization/visualization.h>
#include <ifopt/problem.h>
#include <ifopt/ipopt_solver.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt_ifopt/utils/numeric_differentiation.h>
#include <trajopt_ifopt/constraints/collision/continuous_collision_constraint.h>
#include <trajopt_ifopt/constraints/collision/continuous_collision_evaluators.h>
#include <trajopt_ifopt/constraints/collision/discrete_collision_constraint.h>
#include <trajopt_ifopt/constraints/collision/discrete_collision_evaluators.h>
#include <trajopt_ifopt/constraints/joint_position_constraint.h>
#include <trajopt_ifopt/costs/squared_cost.h>
#include "trajopt_test_utils.hpp"

using namespace trajopt_ifopt;
using namespace tesseract_environment;
using namespace tesseract_kinematics;
using namespace tesseract_collision;
using namespace tesseract_visualization;
using namespace tesseract_scene_graph;
using namespace tesseract_geometry;
using namespace tesseract_common;

class SimpleCollisionTest : public testing::TestWithParam<const char*>
{
public:
  Environment::Ptr env = std::make_shared<Environment>(); /**< Tesseract */
  Visualization::Ptr plotter_;                            /**< Plotter */

  void SetUp() override
  {
    boost::filesystem::path urdf_file(std::string(TRAJOPT_DIR) + "/test/data/spherebot.urdf");
    boost::filesystem::path srdf_file(std::string(TRAJOPT_DIR) + "/test/data/spherebot.srdf");
    ResourceLocator::Ptr locator = std::make_shared<TrajOptSupportResourceLocator>();
    EXPECT_TRUE(env->init(urdf_file, srdf_file, locator));
  }
};

TEST_F(SimpleCollisionTest, spheres)  // NOLINT
{
  CONSOLE_BRIDGE_logDebug("SimpleCollisionTest, spheres");

  std::unordered_map<std::string, double> ipos;
  ipos["spherebot_x_joint"] = -0.75;
  ipos["spherebot_y_joint"] = 0.75;
  env->setState(ipos);

  //  plotter_->plotScene();

  std::vector<ContactResultMap> collisions;
  auto state_solver = env->getStateSolver();
  DiscreteContactManager::Ptr manager = env->getDiscreteContactManager();
  tesseract_kinematics::JointGroup::ConstPtr manip = env->getJointGroup("manipulator");

  manager->setActiveCollisionObjects(manip->getActiveLinkNames());
  manager->setDefaultCollisionMarginData(0);

  collisions.clear();

  // 2) Create the problem
  ifopt::Problem nlp;

  // 3) Add Variables
  std::vector<trajopt_ifopt::JointPosition::ConstPtr> vars;
  std::vector<Eigen::VectorXd> positions;
  {
    Eigen::VectorXd pos(2);
    pos << -0.75, 0.75;
    positions.push_back(pos);
    auto var = std::make_shared<trajopt_ifopt::JointPosition>(pos, manip->getJointNames(), "Joint_Position_0");
    vars.push_back(var);
    nlp.AddVariableSet(var);
  }

  // Step 3: Setup collision
  double margin_coeff = 10;
  double margin = 0.2;
  auto trajopt_collision_config = std::make_shared<trajopt_ifopt::TrajOptCollisionConfig>(margin, margin_coeff);
  trajopt_collision_config->collision_margin_buffer = 0.05;

  auto collision_cache = std::make_shared<trajopt_ifopt::CollisionCache>(100);
  trajopt_ifopt::DiscreteCollisionEvaluator::Ptr collision_evaluator =
      std::make_shared<trajopt_ifopt::SingleTimestepCollisionEvaluator>(
          collision_cache, manip, env, trajopt_collision_config);

  auto cnt = std::make_shared<trajopt_ifopt::DiscreteCollisionConstraint>(collision_evaluator, vars[0], 3);
  nlp.AddConstraintSet(cnt);

  nlp.PrintCurrent();
  std::cout << "Jacobian: \n" << nlp.GetJacobianOfConstraints() << std::endl;

  auto error_calculator = [&](const Eigen::Ref<const Eigen::VectorXd>& x) { return cnt->CalcValues(x); };
  trajopt_ifopt::SparseMatrix num_jac_block = trajopt_ifopt::calcForwardNumJac(error_calculator, positions[0], 1e-4);
  std::cout << "Numerical Jacobian: \n" << num_jac_block << std::endl;

  // 5) choose solver and options
  ifopt::IpoptSolver ipopt;
  ipopt.SetOption("derivative_test", "first-order");
  ipopt.SetOption("linear_solver", "mumps");
  //  ipopt.SetOption("jacobian_approximation", "finite-difference-values");
  ipopt.SetOption("jacobian_approximation", "exact");
  ipopt.SetOption("print_level", 5);

  // 6) solve
  ipopt.Solve(nlp);
  Eigen::VectorXd x = nlp.GetOptVariables()->GetValues();
  std::cout << x.transpose() << std::endl;  // NOLINT

  EXPECT_TRUE(ipopt.GetReturnStatus() == 0);

  tesseract_common::TrajArray inputs(1, 2);
  inputs << -0.75, 0.75;
  Eigen::Map<tesseract_common::TrajArray> results(x.data(), 1, 2);

  bool found =
      checkTrajectory(collisions, *manager, *state_solver, manip->getJointNames(), inputs, *trajopt_collision_config);

  EXPECT_TRUE(found);
  CONSOLE_BRIDGE_logWarn((found) ? ("Initial trajectory is in collision") : ("Initial trajectory is collision free"));

  collisions.clear();
  found =
      checkTrajectory(collisions, *manager, *state_solver, manip->getJointNames(), results, *trajopt_collision_config);

  EXPECT_FALSE(found);
  CONSOLE_BRIDGE_logWarn((found) ? ("Final trajectory is in collision") : ("Final trajectory is collision free"));
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
