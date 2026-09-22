;;; Sierra Script 1.0 - (do not remove this comment)
(script# 950)
(include sci.sh)
(use Main)

; Batch-decompile fixture, part A. global3 and global40 are unused slots in
; the template's Main, so the decompiler sees them as unnamed. This assignment
; can only suggest a name for global3 once global40 has one, which script 951
; (BatchGlobalsB) supplies. In script-number order that takes a second naming
; round.
(public
	batchGlobalsA 0
)

(procedure (batchGlobalsA)
	(= global3 global40)
)
