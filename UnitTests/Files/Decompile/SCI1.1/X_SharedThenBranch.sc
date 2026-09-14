;;; Sierra Script 1.0 - (do not remove this comment)
(script# 903)
(include sci.sh)

; A shared-then shape: a "bnt" whose target is an inner if's then block. That
; is (or (not X) Y) made with a synthesized not, a shape Sierra's compiler does
; not emit. The structurer must not merge it as an and, because that changes
; the value. It must fall back to asm cleanly ("Unstructured branches").
(public
	xSharedThenBranch 0
)

(procedure (xSharedThenBranch &tmp temp0 temp1 temp4)
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
