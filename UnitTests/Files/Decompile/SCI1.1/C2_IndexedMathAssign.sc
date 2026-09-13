;;; Sierra Script 1.0 - (do not remove this comment)
(script# 920)
(include sci.sh)

; A compound assignment to an indexed variable with a simple indexer. The
; compiler emits Sierra's sequence (index; lsti; value; op; push; index; sati)
; so the text round-trips. A complex indexer keeps the older sequence.
(public
	c2IndexedMathAssign 0
)

(procedure (c2IndexedMathAssign param1 param2 &tmp [temp0 4])
	(+= [temp0 param1] param2)
	(-= [temp0 2] 1)
	(return [temp0 param1])
)
