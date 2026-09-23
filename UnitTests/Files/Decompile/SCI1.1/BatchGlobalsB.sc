;;; Sierra Script 1.0 - (do not remove this comment)
(script# 951)
(include sci.sh)
(use Main)

; Batch-decompile fixture, part B. Assigning the unnamed global5 from the
; named gEgo lets the decompiler name global5, which in turn lets script 950
; (BatchGlobalsA) name the global it assigns from global5. (This comment must
; not mention that other global by name: a test scans this file for it.)
(public
	batchGlobalsB 0
)

(procedure (batchGlobalsB)
	(= global5 gEgo)
)
