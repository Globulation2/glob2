// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Absence is temporary, and never proves that a route is inaccessible.
enum class FieldReadiness { Pending, Ready };
enum class RouteResult { Pending, Reachable, Unreachable };
enum class StepResult { Pending, Step, AtGoal, Blocked };
