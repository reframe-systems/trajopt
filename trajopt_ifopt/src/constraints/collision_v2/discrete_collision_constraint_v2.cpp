/**
 * @file discrete_collision_constraint.cpp
 * @brief The single timestep collision position constraint
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

#include <trajopt_utils/macros.h>
TRAJOPT_IGNORE_WARNINGS_PUSH
#include <tesseract_kinematics/core/utils.h>
#include <console_bridge/console.h>
TRAJOPT_IGNORE_WARNINGS_POP

#include <trajopt_ifopt/constraints/collision_v2/discrete_collision_constraint_v2.h>
#include <trajopt_ifopt/constraints/collision/collision_utils.h>
#include <trajopt_ifopt/utils/numeric_differentiation.h>

namespace trajopt_ifopt
{
DiscreteCollisionConstraintIfoptV2::DiscreteCollisionConstraintIfoptV2(
    DiscreteCollisionEvaluator::Ptr collision_evaluator,
    std::vector<tesseract_collision::ObjectPairKey> collision_object_pairs,
    JointPosition::ConstPtr position_var,
    const std::string& name)
  : ifopt::ConstraintSet(static_cast<int>(collision_object_pairs.size()), name)
  , position_var_(std::move(position_var))
  , collision_evaluator_(std::move(collision_evaluator))
  , collision_object_pairs_(std::move(collision_object_pairs))
{
  // Set n_dof_ for convenience
  n_dof_ = position_var_->GetRows();
  assert(n_dof_ > 0);

  bounds_ = std::vector<ifopt::Bounds>(collision_object_pairs_.size(), ifopt::BoundSmallerZero);
}

Eigen::VectorXd DiscreteCollisionConstraintIfoptV2::GetValues() const
{
  // Get current joint values
  Eigen::VectorXd joint_vals = this->GetVariables()->GetComponent(position_var_->GetName())->GetValues();

  return CalcValues(joint_vals);
}

// Set the limits on the constraint values
std::vector<ifopt::Bounds> DiscreteCollisionConstraintIfoptV2::GetBounds() const { return bounds_; }

void DiscreteCollisionConstraintIfoptV2::FillJacobianBlock(std::string var_set, Jacobian& jac_block) const
{
  // Only modify the jacobian if this constraint uses var_set
  if (var_set == position_var_->GetName())
  {
    // Get current joint values
    VectorXd joint_vals = this->GetVariables()->GetComponent(position_var_->GetName())->GetValues();

    CalcJacobianBlock(joint_vals, jac_block);
  }
}

Eigen::VectorXd
DiscreteCollisionConstraintIfoptV2::CalcValues(const Eigen::Ref<const Eigen::VectorXd>& joint_vals) const
{
  // Check the collisions
  CollisionCacheData::ConstPtr collision_data = collision_evaluator_->CalcCollisions(joint_vals);
  double margin_buffer = collision_evaluator_->GetCollisionConfig().collision_margin_buffer;
  Eigen::VectorXd values =
      Eigen::VectorXd::Constant(static_cast<Eigen::Index>(collision_object_pairs_.size()), -margin_buffer);

  for (std::size_t i = 0; i < collision_object_pairs_.size(); ++i)
  {
    const auto& cp = collision_object_pairs_[i];
    auto it = collision_data->gradient_results_set_map.find(cp);
    if (it != collision_data->gradient_results_set_map.end())
      values(static_cast<Eigen::Index>(i)) = getAverageWeightedValuesPost(it->second)[0];
  }

  return values;
}

void DiscreteCollisionConstraintIfoptV2::SetBounds(const std::vector<ifopt::Bounds>& bounds)
{
  assert(bounds.size() == 1);
  bounds_ = bounds;
}

void DiscreteCollisionConstraintIfoptV2::CalcJacobianBlock(const Eigen::Ref<const Eigen::VectorXd>& joint_vals,
                                                           Jacobian& jac_block) const
{
  // Calculate collisions
  CollisionCacheData::ConstPtr collision_data = collision_evaluator_->CalcCollisions(joint_vals);
  jac_block.reserve(static_cast<Eigen::Index>(collision_data->contact_results_map.size()) * position_var_->GetRows());

  for (Eigen::Index i = 0; i < static_cast<Eigen::Index>(collision_object_pairs_.size()); ++i)
  {
    const auto& cp = collision_object_pairs_[static_cast<std::size_t>(i)];
    auto it = collision_data->gradient_results_set_map.find(cp);

    // TODO should a tuple be used here?
    if (it != collision_data->gradient_results_set_map.end())
    {
      Eigen::VectorXd grad_vec = getWeightedAvgGradientPost(it->second);

      // Collision is 1 x n_dof
      for (int j = 0; j < it->second.dof; j++)
        jac_block.coeffRef(i, j) = -1.0 * grad_vec[j];
    }
  }
}

DiscreteCollisionEvaluator::Ptr DiscreteCollisionConstraintIfoptV2::GetCollisionEvaluator() const
{
  return collision_evaluator_;
}

}  // namespace trajopt_ifopt