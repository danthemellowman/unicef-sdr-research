// This implements the helper for QObject.pyqtConfigure().
//
// Copyright (c) 2013 Riverbank Computing Limited <info@riverbankcomputing.com>
// 
// This file is part of PyQt.
// 
// This file may be used under the terms of the GNU General Public
// License versions 2.0 or 3.0 as published by the Free Software
// Foundation and appearing in the files LICENSE.GPL2 and LICENSE.GPL3
// included in the packaging of this file.  Alternatively you may (at
// your option) use any later version of the GNU General Public
// License if such license has been publicly approved by Riverbank
// Computing Limited (or its successors, if any) and the KDE Free Qt
// Foundation. In addition, as a special exception, Riverbank gives you
// certain additional rights. These rights are described in the Riverbank
// GPL Exception version 1.1, which can be found in the file
// GPL_EXCEPTION.txt in this package.
// 
// If you are unsure which license is appropriate for your use, please
// contact the sales department at sales@riverbankcomputing.com.
// 
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.


#include <Python.h>

#include <QByteArray>
#include <QMetaObject>
#include <QObject>
#include <QVariant>

#include "qpycore_chimera.h"
#include "qpycore_pyqtboundsignal.h"
#include "qpycore_sip.h"


// The result of handling a keyword argument.
enum ArgStatus
{
    AsError,
    AsHandled,
    AsUnknown
};


static ArgStatus handle_argument(PyObject *self, QObject *qobj,
        PyObject *name_obj, PyObject *value_obj);


// This is the helper for QObject.pyqtConfigure().
PyObject *qpycore_pyqtconfigure(PyObject *self, PyObject *args, PyObject *kwds)
{
    // Check there are no positional arguments.
    if (PyTuple_Size(args) > 0)
    {
        PyErr_SetString(PyExc_TypeError,
                "QObject.pyqtConfigure() has no positional arguments");
        return 0;
    }

    // Get the QObject self.
    QObject *qobj = reinterpret_cast<QObject *>(
            sipGetCppPtr((sipSimpleWrapper *)self, sipType_QObject));

    if (!qobj)
        return 0;

    PyObject *name_obj, *value_obj;
    SIP_SSIZE_T pos = 0;

    while (PyDict_Next(kwds, &pos, &name_obj, &value_obj))
    {
        ArgStatus as = handle_argument(self, qobj, name_obj, value_obj);

        if (as == AsError)
            return 0;

        if (as == AsUnknown)
        {
            PyErr_Format(PyExc_AttributeError,
                    "'%S' is not a Qt property or a signal", name_obj);

            return 0;
        }
    }

    Py_INCREF(Py_None);
    return Py_None;
}


// This is the helper for the QObject %FinalisationCode.
int qpycore_qobject_finalisation(PyObject *self, QObject *qobj, PyObject *kwds,
        PyObject **updated_kwds)
{
    // Handle the trivial case.
    if (!kwds)
        return 0;

    // The dict we will be removing handled arguments from, 0 if it needs to be
    // created.
    PyObject *unused = (updated_kwds ? 0 : kwds);

    PyObject *name_obj, *value_obj;
    SIP_SSIZE_T pos = 0;

    while (PyDict_Next(kwds, &pos, &name_obj, &value_obj))
    {
        ArgStatus as = handle_argument(self, qobj, name_obj, value_obj);

        if (as == AsError)
            return -1;

        if (as == AsHandled)
        {
            if (!unused)
            {
                unused = PyDict_Copy(kwds);

                if (!unused)
                    return -1;

                *updated_kwds = unused;
            }

            if (PyDict_DelItem(unused, name_obj) < 0)
            {
                if (updated_kwds)
                    Py_DECREF(unused);

                return -1;
            }
        }
    }

    return 0;
}


// Handle a single keyword argument.
static ArgStatus handle_argument(PyObject *self, QObject *qobj,
        PyObject *name_obj, PyObject *value_obj)
{
    const QMetaObject *mo = qobj->metaObject();

    // Get the name encoded name.
    PyObject *enc_name_obj = name_obj;
    const char *name = sipString_AsASCIIString(&enc_name_obj);

    if (!name)
        return AsError;

    QByteArray enc_name(name);
    Py_DECREF(enc_name_obj);

    // See if it is a property.
    int idx = mo->indexOfProperty(enc_name.constData());

    if (idx >= 0)
    {
        QMetaProperty prop = mo->property(idx);

        // A negative type means a QVariant property.
        if (prop.userType() >= 0)
        {
            const Chimera *ct = Chimera::parse(prop);

            if (!ct)
            {
                PyErr_Format(PyExc_TypeError,
                        "'%s' keyword argument has an invalid type",
                        enc_name.constData());

                return AsError;
            }

            QVariant value;
            bool valid = ct->fromPyObject(value_obj, &value);

            delete ct;

            if (!valid)
                return AsError;

            qobj->setProperty(enc_name.constData(), value);
        }
        else
        {
            int value_state, iserr = 0;

            QVariant *value = reinterpret_cast<QVariant *>(
                    sipForceConvertToType(value_obj, sipType_QVariant, 0,
                            SIP_NOT_NONE, &value_state, &iserr));

            if (iserr)
                return AsError;

            qobj->setProperty(enc_name.constData(), *value);

            sipReleaseType(value, sipType_QVariant, value_state);
        }
    }
    else
    {
        bool unknown = true;

        // See if it is a signal.
        PyObject *sig = PyObject_GetAttr(self, name_obj);

        if (sig)
        {
            if (PyObject_TypeCheck(sig, &qpycore_pyqtBoundSignal_Type))
            {
                static PyObject *connect_obj = NULL;

                if (!connect_obj)
                {
#if PY_MAJOR_VERSION >= 3
                    connect_obj = PyUnicode_FromString("connect");
#else
                    connect_obj = PyString_FromString("connect");
#endif

                    if (!connect_obj)
                    {
                        Py_DECREF(sig);
                        return AsError;
                    }
                }

                // Connect the slot.
                PyObject *res = PyObject_CallMethodObjArgs(sig, connect_obj,
                        value_obj, 0);

                if (!res)
                {
                    Py_DECREF(sig);
                    return AsError;
                }

                Py_DECREF(res);

                unknown = false;
            }

            Py_DECREF(sig);
        }

        if (unknown)
        {
            PyErr_Clear();
            return AsUnknown;
        }
    }

    return AsHandled;
}
