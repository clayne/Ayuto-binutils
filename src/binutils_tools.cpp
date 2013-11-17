/**
* =============================================================================
* binutils
* Copyright(C) 2013 Ayuto. All rights reserved.
* =============================================================================
*
* This program is free software; you can redistribute it and/or modify it under
* the terms of the GNU General Public License, version 3.0, as published by the
* Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
* details.
*
* You should have received a copy of the GNU General Public License along with
* this program.  If not, see <http://www.gnu.org/licenses/>.
**/

// ============================================================================
// >> INCLUDES
// ============================================================================
#include <stdlib.h>
#include <string>

#include "dyncall.h"
#include "dyncall_signature.h"

#include "binutils_tools.h"
#include "binutils_macros.h"
#include "binutils_hooks.h"


DCCallVM* g_pCallVM = dcNewCallVM(4096);
extern std::map<CHook *, std::map<DynamicHooks::HookType_t, std::list<PyObject *> > > g_mapCallbacks;

CHookManager* g_pHookMngr = GetHookManager();

inline size_t UTIL_GetSize(void* ptr)
{
#ifdef _WIN32
	return _msize(ptr);
#elif defined(__linux__)
	return malloc_usable_size(ptr);
#else
	#error "Implement me!"
#endif
}

// ============================================================================
// CPointer class
// ============================================================================
CPointer::CPointer(unsigned long ulAddr /* = 0 */)
{
	m_ulAddr = ulAddr;
}

CPointer* CPointer::Add(int iValue)
{
	return new CPointer(m_ulAddr + iValue);
}

CPointer* CPointer::Sub(int iValue)
{
	return Add(-iValue);
}

const char * CPointer::GetString(int iOffset /* = 0 */, bool bIsPtr /* = true */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	if (bIsPtr)
		return Get<char *>(iOffset);

	return (char *) (m_ulAddr + iOffset);
}

void CPointer::SetString(char* szText, int iSize /* = 0 */, int iOffset /* = 0 */, bool bIsPtr /* = true */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	if (!iSize)
	{
		iSize = UTIL_GetSize((void *) (m_ulAddr + iOffset));
		if(!iSize)
			BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Unable to retrieve size of address.")
	}

	if ((int ) strlen(szText) > iSize)
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "String exceeds size of memory block.")

	if (bIsPtr)
		Set<char *>(szText, iOffset);
	else
		strcpy((char *) (m_ulAddr + iOffset), szText);
}

CPointer* CPointer::GetPtr(int iOffset /* = 0 */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	return new CPointer(*(unsigned long *) (m_ulAddr + iOffset));
}

void CPointer::SetPtr(CPointer* ptr, int iOffset /* = 0 */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	*(unsigned long *) m_ulAddr = ptr->GetAddress();
}

unsigned long CPointer::GetSize()
{
	return UTIL_GetSize((void *) m_ulAddr);
}

CPointer* CPointer::GetVirtualFunc(int iIndex, bool bPlatformCheck /* = true */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

#ifdef __linux__
	if (bPlatformCheck)
		iIndex++;
#endif

	void** vtable = *(void ***) m_ulAddr;
	if (!vtable)
		return new CPointer();

	return new CPointer((unsigned long) vtable[iIndex]);
}

void CPointer::Realloc(unsigned long ulSize)
{
	m_ulAddr = (unsigned long) realloc((void *) m_ulAddr, ulSize);
}

void CPointer::Dealloc()
{
	free((void *) m_ulAddr);
	m_ulAddr = 0;
}

CFunction* CPointer::MakeFunction(Convention_t eConv, char* szParams)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	return new CFunction(m_ulAddr, eConv, szParams);
}

// ============================================================================
// CFunction class
// ============================================================================
CFunction::CFunction(unsigned long ulAddr, Convention_t eConv, char* szParams)
{
	m_ulAddr = ulAddr;
	m_eConv = eConv;
	m_szParams = szParams;
}

object CFunction::__call__(object args)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	dcReset(g_pCallVM);
	dcMode(g_pCallVM, GetDynCallConvention(m_eConv));
	char* ptr = m_szParams;
	int pos = 0;
	char ch;
	while ((ch = *ptr) != '\0' && ch != ')')
	{
		if (ch == DC_SIGCHAR_VOID)
		{
			ptr++;
			break;
		}

		object arg = args[pos];
		switch(ch)
		{
			case DC_SIGCHAR_BOOL:      dcArgBool(g_pCallVM, extract<bool>(arg)); break;
			case DC_SIGCHAR_CHAR:      dcArgChar(g_pCallVM, extract<char>(arg)); break;
			case DC_SIGCHAR_UCHAR:     dcArgChar(g_pCallVM, extract<unsigned char>(arg)); break;
			case DC_SIGCHAR_SHORT:     dcArgShort(g_pCallVM, extract<short>(arg)); break;
			case DC_SIGCHAR_USHORT:    dcArgShort(g_pCallVM, extract<unsigned short>(arg)); break;
			case DC_SIGCHAR_INT:       dcArgInt(g_pCallVM, extract<int>(arg)); break;
			case DC_SIGCHAR_UINT:      dcArgInt(g_pCallVM, extract<unsigned int>(arg)); break;
			case DC_SIGCHAR_LONG:      dcArgLong(g_pCallVM, extract<long>(arg)); break;
			case DC_SIGCHAR_ULONG:     dcArgLong(g_pCallVM, extract<unsigned long>(arg)); break;
			case DC_SIGCHAR_LONGLONG:  dcArgLongLong(g_pCallVM, extract<long long>(arg)); break;
			case DC_SIGCHAR_ULONGLONG: dcArgLongLong(g_pCallVM, extract<unsigned long long>(arg)); break;
			case DC_SIGCHAR_FLOAT:     dcArgFloat(g_pCallVM, extract<float>(arg)); break;
			case DC_SIGCHAR_DOUBLE:    dcArgDouble(g_pCallVM, extract<double>(arg)); break;
			case DC_SIGCHAR_POINTER:   dcArgPointer(g_pCallVM, ExtractPyPtr(arg)); break;
			case DC_SIGCHAR_STRING:    dcArgPointer(g_pCallVM, (unsigned long) (void *) extract<char *>(arg)); break;
			default: BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Unknown parameter type.")
		}
		pos++; ptr++;
	}

	if (pos != len(args))
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "String parameter count does not equal with length of tuple.")

	if (ch == '\0')
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "String parameter has no return type.")

	switch(*++ptr)
	{
		case DC_SIGCHAR_VOID: dcCallVoid(g_pCallVM, m_ulAddr); break;
		case DC_SIGCHAR_BOOL:      return object(dcCallBool(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_CHAR:      return object(dcCallChar(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_UCHAR:     return object((unsigned char) dcCallChar(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_SHORT:     return object(dcCallShort(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_USHORT:    return object((unsigned short) dcCallShort(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_INT:       return object(dcCallInt(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_UINT:      return object((unsigned int) dcCallInt(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_LONG:      return object(dcCallLong(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_ULONG:     return object((unsigned long) dcCallLong(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_LONGLONG:  return object(dcCallLongLong(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_ULONGLONG: return object((unsigned long long) dcCallLongLong(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_FLOAT:     return object(dcCallFloat(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_DOUBLE:    return object(dcCallDouble(g_pCallVM, m_ulAddr));
		case DC_SIGCHAR_POINTER:   return object(CPointer(dcCallPointer(g_pCallVM, m_ulAddr)));
		case DC_SIGCHAR_STRING:    return object((const char *) dcCallPointer(g_pCallVM, m_ulAddr));
		default: BOOST_RAISE_EXCEPTION(PyExc_TypeError, "Unknown return type.")
	}
	return object();
}

object CFunction::CallTrampoline(object args)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	CHook* pHook = g_pHookMngr->FindHook((void *) m_ulAddr);
	if (!pHook)
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function was not hooked.")

	return CFunction((unsigned long) pHook->m_pTrampoline, m_eConv, m_szParams).__call__(args);
}

void CFunction::Hook(DynamicHooks::HookType_t eType, PyObject* pCallable)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	CHook* pHook = g_pHookMngr->HookFunction((void *) m_ulAddr, m_eConv, m_szParams);
	pHook->AddCallback(eType, (void *) &binutils_HookHandler);
	g_mapCallbacks[pHook][eType].push_back(pCallable);
}

void CFunction::Unhook(DynamicHooks::HookType_t eType, PyObject* pCallable)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	CHook* pHook = g_pHookMngr->FindHook((void *) m_ulAddr);
	if (!pHook)
		return;

	g_mapCallbacks[pHook][eType].remove(pCallable);
}

void CFunction::AddPreHook(PyObject* pCallable)
{
	Hook(HOOKTYPE_PRE, pCallable);
}

void CFunction::AddPostHook(PyObject* pCallable)
{
	Hook(HOOKTYPE_POST, pCallable);
}

void CFunction::RemovePreHook(PyObject* pCallable)
{
	Unhook(HOOKTYPE_PRE, pCallable);
}

void CFunction::RemovePostHook(PyObject* pCallable)
{
	Unhook(HOOKTYPE_POST, pCallable);
}

// ============================================================================
// >> FUNCTIONS
// ============================================================================
int GetError()
{
	return dcGetError(g_pCallVM);
}