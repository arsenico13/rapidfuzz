/* SPDX-License-Identifier: MIT */
/* Copyright © 2020 Max Bachmann */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <nonstd/string_view.hpp>
#include <variant/variant.hpp>

#if PY_VERSION_HEX < 0x030C0000
#define PY_BELOW_3_12
#endif

bool valid_str(PyObject* str, const char* name) {
    if (!PyUnicode_Check(str)) {
        PyErr_Format(PyExc_TypeError, "%s must be a String or None", name);
        return false;
    }

    // PyUnicode_READY deprecated in Python 3.10 removed in Python 3.12
#ifdef PY_BELOW_3_12
    if (PyUnicode_READY(str)) {
        return false;
    }
#endif

return true;
}

#define PY_INIT_MOD(name, doc, methods) \
    static struct PyModuleDef moduledef = { \
        PyModuleDef_HEAD_INIT, #name, doc, -1, methods, NULL, NULL, NULL, NULL}; \
    PyMODINIT_FUNC PyInit_##name(void) { \
        return PyModule_Create(&moduledef); \
    }

using python_string_view  = mpark::variant<
  nonstd::basic_string_view<uint8_t>,
  nonstd::basic_string_view<uint16_t>,
  nonstd::basic_string_view<uint32_t>
>;

python_string_view decode_python_string(PyObject* py_str) {
    Py_ssize_t len = PyUnicode_GET_LENGTH(py_str);
    void* str = PyUnicode_DATA(py_str);

    int str_kind = PyUnicode_KIND(py_str);

    switch (str_kind) {
    case PyUnicode_1BYTE_KIND:
        return nonstd::basic_string_view<uint8_t>(static_cast<uint8_t*>(str), len);
    case PyUnicode_2BYTE_KIND:
        return nonstd::basic_string_view<uint16_t>(static_cast<uint16_t*>(str), len);
    default:
        return nonstd::basic_string_view<uint32_t>(static_cast<uint32_t*>(str), len);
    }
}
