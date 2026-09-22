;;; Sierra Script 1.0 - (do not remove this comment)
(script# 950)
(include sci.sh)
(use Main)

; Batch-decompile fixture, part A. The test leaves slot 3 of the template's
; Main under its standard name (global3) and renames slot 5 to its standard
; name (global5) in Main.sco before compiling, so the decompiler sees both as
; unnamed. This assignment can only suggest a name for global3 once global5
; has one, which script 951 (BatchGlobalsB) supplies. In script-number order
; that takes a second naming round.
(public
	batchGlobalsA 0
)

(procedure (batchGlobalsA)
	(= global3 global5)
)
