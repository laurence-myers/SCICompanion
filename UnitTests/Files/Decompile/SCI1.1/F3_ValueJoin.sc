;;; Sierra Script 1.0 - (do not remove this comment)
(script# 903)
(include sci.sh)

; Family 3: an and/or value is materialised through branches, then stored.
; The (!X or Y) shape is disabled in the decompiler, so its value join stays a
; bare two-way node. The if-follow search then fails: "Exit needs two
; predecessors".
(public
	f3ValueJoin 0
)

(procedure (f3ValueJoin &tmp temp0 temp1 temp4)
	(asm
		; (= temp4 (or (not temp0) temp1)) materialised as 1 or 0
		lat temp0
		bnt isTrue
		lat temp1
		bnt isFalse
	isTrue:
		ldi 1
		jmp store1
	isFalse:
		ldi 0
	store1:
		sat temp4
		ret
	)
)
