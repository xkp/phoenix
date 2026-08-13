#pragma once

// Narrow replacement for the production application's precompiled-header
// umbrella. Copied partition translation units keep their original include;
// only the dependencies actually required by the partition subsystem enter.

#include "phoenix/partition/ported/production_probe_compat.hpp"
#include "phoenix/partition/ported/null_diagnostics.hpp"
#include "phoenix/partition/ported/vm_compat.hpp"
#include "phoenix/partition/geometry.h"

#include <boost/function.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/variant.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <ctime>
#include <functional>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

