;;; Sierra Script 1.0 - (do not remove this comment)
(script# 923)
(include sci.sh)

; Chained comparisons. Sierra compiles (< 0 x 19) with a pprev so the middle
; is evaluated once; the decompiler folds the two halves back into one n-ary
; comparison. A comparison whose operands are not shared stays an and.
(public
	n1ChainedCompare 0
)

(procedure (n1ChainedCompare param1 &tmp temp0)
	(= temp0 0)
	(if (< 0 param1 19)
		(= temp0 1)
	)
	(if (<= 1 param1 15)
		(++ temp0)
	)
	(if (and (< param1 5) (> temp0 10))
		(= temp0 2)
	)
	(return (< 5 param1 10))
)
