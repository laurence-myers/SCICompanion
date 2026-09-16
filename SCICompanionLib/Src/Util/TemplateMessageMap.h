#pragma once

//
// Shared template message-map macros.
//
// BEGIN_TEMPLATE_MESSAGE_MAP_2 and the TEMPLATE_*/TCLASS_* helpers let a class
// template declare an MFC message map. They were previously copied verbatim into
// the stdafx.h of all three projects (SCICompanionLib, SCICompanion, UnitTests),
// so a change to one copy could silently drift from the others. They now live in
// this one header, which each stdafx.h includes.
//
// This header is macro text only. The MFC symbols the macros expand to
// (PTM_WARNING_DISABLE, AFX_MSGMAP, AFX_MSGMAP_ENTRY, PASCAL) come from the MFC
// headers each stdafx.h pulls in before this include, and from the expansion site
// (see CUndoResource in UndoResource.h, the sole user of the _2 macro).
//
// #pragma warning balance: this macro pairs with MFC's END_MESSAGE_MAP, which
// emits TWO pops -- an inner __pragma(warning(pop)) plus PTM_WARNING_RESTORE. So
// it must emit TWO pushes to match, exactly as MFC's own BEGIN_MESSAGE_MAP does:
// PTM_WARNING_DISABLE (a push) and an inner __pragma(warning(push)). The inner
// push also disables C4640 for the static message-entry array, again as MFC does.
// Without the inner push the END_MESSAGE_MAP pop is unmatched and every TU that
// expands this macro emits C4193.
//

#define BEGIN_TEMPLATE_MESSAGE_MAP_2(theClass, type_name1, type_name2, baseClass) \
    PTM_WARNING_DISABLE \
    template < typename type_name1, typename type_name2 > \
    const AFX_MSGMAP* theClass< type_name1, type_name2 >::GetMessageMap() const \
        { return GetThisMessageMap(); } \
    template < typename type_name1, typename type_name2 > \
    const AFX_MSGMAP* PASCAL theClass< type_name1, type_name2 >::GetThisMessageMap() \
    { \
        typedef theClass< type_name1, type_name2 > ThisClass; \
        typedef baseClass TheBaseClass; \
        __pragma(warning(push)) \
        __pragma(warning(disable: 4640)) /* message maps can only be called by single threaded message pump */ \
        static const AFX_MSGMAP_ENTRY _messageEntries[] = \
        {

// Additional defines so we can use multi-value templates with BEGIN_TEMPLATE_MESSAGE_MAP
#define TEMPLATE_1(t1)                   t1
#define TEMPLATE_2(t1, t2)               t1, t2
#define TEMPLATE_3(t1 ,t2 ,t3)           t1, t2, t3
#define TCLASS_1(theClass, t1)           theClass<t1>
#define TCLASS_2(theClass, t1, t2)       theClass<t1, t2>
#define TCLASS_3(theClass, t1, t2, t3)   theClass<t1, t2, t3>
