#include <imagejxx/results_table_state.hpp>
#include "binding_metadata.hpp"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <limits>

namespace py=pybind11;
using namespace imagejxx;
namespace {
std::size_t index(py::handle value) {
  if(py::isinstance<py::bool_>(value) || !py::isinstance<py::int_>(value))throw py::type_error("Expected an integer index");
  const auto result=PyLong_AsUnsignedLongLong(value.ptr());if(PyErr_Occurred())throw py::error_already_set();
  if(result>std::numeric_limits<std::size_t>::max())throw std::overflow_error("Results table index overflow");
  return std::size_t(result);
}
void charge(std::size_t& bytes,std::size_t amount,std::size_t limit) {
  if(amount>limit || bytes>limit-amount)throw std::invalid_argument("Results table input exceeds working byte limit");bytes+=amount;
}
// Bound conversion before the existing metadata decoder owns a second tree.
void record_bytes(py::handle value,std::size_t& bytes,std::size_t limit,unsigned depth=0) {
  if(depth>16)throw std::invalid_argument("Results table record is too deeply nested");
  if((py::isinstance<py::dict>(value) && !PyDict_CheckExact(value.ptr())) ||
      (py::isinstance<py::list>(value) && !PyList_CheckExact(value.ptr())) ||
      (py::isinstance<py::tuple>(value) && !PyTuple_CheckExact(value.ptr())))
    throw py::type_error("Results table record requires plain containers");
  charge(bytes,128,limit);
  if(py::isinstance<py::str>(value)) {
    const auto size=PyUnicode_GetLength(value.ptr());if(size<0)throw py::error_already_set();
    if(std::size_t(size)>limit/4)throw std::invalid_argument("Results table input text exceeds working byte limit");
    charge(bytes,std::size_t(size)*4,limit);
  }else if(py::isinstance<py::dict>(value)) {
    for(auto item:py::reinterpret_borrow<py::dict>(value)){record_bytes(item.first,bytes,limit,depth+1);record_bytes(item.second,bytes,limit,depth+1);}
  }else if(py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value)) {
    const auto seq=py::reinterpret_borrow<py::sequence>(value);
    if(std::size_t(py::len(seq))>limit/128)throw std::invalid_argument("Results table input has too many nodes");
    for(auto item:seq)record_bytes(item,bytes,limit,depth+1);
  }
}
}
void bind_results_table_state(py::module_& m) {
  py::class_<ResultsTableStateLimits>(m,"ResultsTableStateLimits")
    .def(py::init<>()).def_readwrite("max_rows",&ResultsTableStateLimits::max_rows)
    .def_readwrite("max_columns",&ResultsTableStateLimits::max_columns)
    .def_readwrite("max_name_bytes",&ResultsTableStateLimits::max_name_bytes)
    .def_readwrite("max_working_bytes",&ResultsTableStateLimits::max_working_bytes);
  py::class_<ResultsTableRows>(m,"ResultsTableRows")
    .def(py::init([](py::object rows,std::vector<std::string> columns,bool managed){return ResultsTableRows{index(rows),std::move(columns),managed};}),
      py::arg("rows")=1,py::arg("numeric_columns")=std::vector<std::string>{},py::arg("managed")=true)
    .def_readwrite("rows",&ResultsTableRows::rows).def_readwrite("numeric_columns",&ResultsTableRows::numeric_columns)
    .def_readwrite("managed",&ResultsTableRows::managed);
  py::class_<ResultsTableState>(m,"ResultsTableState")
    .def(py::init<ResultsTableStateLimits>(),py::arg("limits")=ResultsTableStateLimits{})
    .def_property_readonly("row_count",&ResultsTableState::row_count)
    .def_property_readonly("capacity",&ResultsTableState::capacity)
    .def_property_readonly("estimated_bytes",&ResultsTableState::estimated_bytes)
    .def_property_readonly("column_names",&ResultsTableState::column_names)
    .def_property("nan_empty_cells",&ResultsTableState::nan_empty_cells,&ResultsTableState::set_nan_empty_cells)
    .def("clear",&ResultsTableState::clear)
    .def("default_value",[](const ResultsTableState& s,py::object row,const std::string& name){return s.default_value(index(row),name);},py::arg("row"),py::arg("column"))
    .def("append_rows",[](ResultsTableState& s,py::sequence inputs,bool flag) {
      if(!PyList_CheckExact(inputs.ptr()) && !PyTuple_CheckExact(inputs.ptr()))
        throw py::type_error("Results table groups require a plain list or tuple");
      const auto limit=s.limits().max_working_bytes;std::size_t bytes=0;
      const auto count=std::size_t(py::len(inputs));if(count>limit/sizeof(ResultsTableRows))throw std::invalid_argument("Results table input has too many groups");
      charge(bytes,count*sizeof(ResultsTableRows),limit);
      for(auto item:inputs) {
        const auto& group=py::cast<const ResultsTableRows&>(item);
        for(const auto& name:group.numeric_columns){charge(bytes,sizeof(std::string),limit);if(name.size()>15){charge(bytes,name.size(),limit);charge(bytes,32,limit);}}
      }
      auto groups=py::cast<std::vector<ResultsTableRows>>(inputs);s.append_rows(groups,flag,bytes);
    },py::arg("groups"),py::arg("nan_empty_cells"))
    .def_property_readonly("record",[](const ResultsTableState& s){return bindings::metadata_python(s.record());})
    .def_static("from_record",[](py::dict record,ResultsTableStateLimits limits) {
      std::size_t bytes=0;record_bytes(record,bytes,limits.max_working_bytes);
      if(bytes>limits.max_working_bytes/2)throw std::invalid_argument("Results table record conversion exceeds working byte limit");
      auto source=bindings::metadata_object_value(record);
      return ResultsTableState::from_record(source,limits,bytes);
    },py::arg("record"),py::arg("limits")=ResultsTableStateLimits{});
}
