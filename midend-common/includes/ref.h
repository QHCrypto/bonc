#pragma once

#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace bonc {

template <typename T>
using Ref = boost::intrusive_ptr<T>;

}