//
// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.md file in the project root for full license information.
//
// Criterion.h -- helper classes for accumulating criteria

#pragma once

#include "Basics.h"
#include "Matrix.h"
#include <memory> // for pair
#include <limits> // for isnan() and numeric_limits  --TODO: is that the right header?

namespace Microsoft { namespace MSR { namespace CNTK {

// helper class for passing accumulated epoch-level criteria around while retaining their sample counts
// Criteria are represented as a tuple (aggregate criterion, sample count). The average criterion value is their ratio.
struct EpochCriterion : public std::pair<double, size_t>
{
    // construction
    explicit EpochCriterion(double aggregateCriterionValue = 0.0, size_t aggregateSampleCount = 0) : std::pair<double, size_t>(aggregateCriterionValue, aggregateSampleCount) { }
    EpochCriterion(const std::pair<double, size_t>& other) : std::pair<double, size_t>(other) { }

    // main way of reading this out: compute the actual average criterion value from the aggregate and sample count
    double Average() const { return second > 0 ? first / second : 0.0; } // compute the epoch-average

    // a few more handy operations that occured multiple times
    bool IsNan() const { return std::isnan(first); }
    EpochCriterion operator-(const EpochCriterion& other) const { return EpochCriterion(first - other.first, second - other.second); }
    void operator+=(const EpochCriterion& other) { first += other.first; second += other.second; }

    static EpochCriterion Infinity() { return EpochCriterion(std::numeric_limits<double>::infinity()); }
    bool IsInfinity() const { return first == std::numeric_limits<double>::infinity(); }
};

// We accumulate criteria in this struct.
// Criteria are accumulated together with their counts (counts depend on sequence lengths, and different criteria may have different sequence lengths).
template <class ElemType>
struct CriterionAccumulator
{
    // constructor
    CriterionAccumulator(size_t numCriteria, DEVICEID_TYPE deviceId) :
        m_aggregateCriterionValues(1, numCriteria, deviceId)
    {
        m_aggregateCriterionValues.SetValue(0);
        m_aggregateSampleCounts.assign(numCriteria, 0);
    }
    // 'i' is the index of the element we add into (multiple eval criteria share the same matrix object)
    void Accumulate(const std::vector<ComputationNodeBasePtr>& nodes, size_t i, size_t legacyNumSamples)
    {
        const auto& node = nodes[i]; // multiple nodes are managed by this struct
        // Note: A future change will be that criterion nodes emit criteria per frame, but aggregated.
        // In that case, the denominator will be accumulated from their MBLayout.
        // Also, the numerator will have masking and an implicit reduction.
        Matrix<ElemType>::AddElementToElement(dynamic_pointer_cast<ComputationNode<ElemType>>(node)->Value(),
                                              0, 0, m_aggregateCriterionValues, 0, i);
        m_aggregateSampleCounts[i] += GetNumSamples(nodes[i], legacyNumSamples);
    }
    // retrieve an accumulated result as a pair (numerator, denominator)
    EpochCriterion GetCriterion(size_t i) const
    {
        return EpochCriterion(m_aggregateCriterionValues(0, i), m_aggregateSampleCounts[i]);
    }
    // retrive a result from a node
    static EpochCriterion GetCriterion(const ComputationNodeBasePtr& node, size_t legacyNumSamples)
    {
        auto numSamples = GetNumSamples(node, legacyNumSamples);
        return numSamples > 0 ? EpochCriterion(node->Get00Element(), numSamples) : EpochCriterion(0); // (avoid GPU access if 0 samples)
    }

private:
    // get the number of samples
    static size_t GetNumSamples(const ComputationNodeBasePtr& node, size_t legacyNumSamples)
    {
        if (node->HasMBLayout())
            return node->GetMBLayout()->GetActualNumSamples();
        else
            return legacyNumSamples;
    }

private:
    Matrix<ElemType> m_aggregateCriterionValues; // [1 x N]
    vector<size_t> m_aggregateSampleCounts;      // [N]
};

}}}