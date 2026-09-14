;;; Sierra Script 1.0 - (do not remove this comment)
(script# 917)
(include sci.sh)

; Compound conditions compiled by SCI Companion's own compiler. Its "bt" lands
; on the then block, not the join "bnt", so this also covers the unchain
; fixup end to end. The decompiled text must equal this source.
(public
	p1CompoundConditions 0
)

(procedure (p1CompoundConditions &tmp temp0 temp1 temp2 temp3)
	(if (and temp0 temp1)
		(= temp3 1)
	)
	(if (or temp0 (and temp1 temp2))
		(= temp3 2)
	)
	(if (and temp0 (or temp1 temp2))
		(= temp3 3)
	)
	(if (or temp0 temp1 temp2)
		(= temp3 4)
	)
	(if (not (and temp0 temp1))
		(= temp3 5)
	)
	(while (and temp0 temp1)
		(= temp3 6)
	)
	(while (or temp0 temp1)
		(= temp3 7)
	)
	(return (and temp0 temp1))
)
