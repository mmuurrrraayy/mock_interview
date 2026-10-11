#ifndef REQUEST_PARSER_H
#define REQUEST_PARSER_H

#include <string>
#include <vector>
#include "job.h"

/*
 * Parse a request JSON document into Jobs, one per stage, in stage order.
 *
 * Mapping:
 *   stage.id                -> Job id
 *   stage.type              -> processor ("GPU" -> true, "CPU" -> false)
 *   stage.resources.ram_gb  -> memory (GB)
 *   stage.duration          -> duration (JSON is in MILLISECONDS)
 *   stage.dependents        -> pair<int,int>: 1-based stage numbers (position
 *                              in the "stages" array), unused slots are 0,
 *                              order preserved.
 *   All other fields are ignored.
 *
 * Throws std::invalid_argument if:
 *   - the text is not valid JSON
 *   - "stages" is missing or not an array
 *   - a stage is missing id / type / resources.ram_gb / duration / dependents
 *   - type is not "CPU" or "GPU"
 *   - a stage has more than 2 dependents
 *   - a dependent is < 1 or > number of stages
 */
std::vector<Job> parse_request(const std::string& json_text);

/* Optional helper the candidate may find useful. Not tested directly. */
// Job parse_stage(const nlohmann::json& stage);

#endif