;;; Sierra Script 1.0 - (do not remove this comment)
(script# 920)
(include sci.sh)

; A compound assignment to an indexed variable. Sierra evaluates the indexer
; twice, even a complex one like (+ param1 1), so the compiler emits that
; sequence and the decompiler folds the two reads back into a compound
;  assignment. Simple and complex indexers both round-trip. Used as a value,
; the assignment gives the new value: "sati" pops it into the accumulator.
(public
	c2IndexedMathAssign 0
)

(procedure (c2IndexedMathAssign param1 param2 &tmp [temp0 4] temp4)
	(+= [temp0 param1] param2)
	(-= [temp0 2] 1)
	(+= [temp0 (+ param1 1)] param2)
	(|= [temp0 (/ param1 16)] param2)
	(= temp4 (+= [temp0 param1] param2))
)
