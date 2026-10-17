#pragma once

#include <nlohmann/json.hpp>

// Self-describing documentation for the control channel.
//
// Served by the `help`, `describe_action`, `describe_query` and `describe_op`
// ops so a client can drive the DAW without reading its source: what every
// action/query takes, what the arguments mean, and a worked example of each.
// The shared vocabulary (managerPath, node ids, pitch/rhythm vectors, tuning)
// lives in controlConcepts() and is included in every `help` reply.
// All four return references to function-local statics, so callers may hold
// proxies (e.g. `.items()`) over them without a temporary dying mid-expression.
const nlohmann::json& controlConcepts();
const nlohmann::json& actionDocs();
const nlohmann::json& queryDocs();
const nlohmann::json& opDocs();
