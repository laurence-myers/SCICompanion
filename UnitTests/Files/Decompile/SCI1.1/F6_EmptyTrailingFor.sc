;;; Sierra Script 1.0 - (do not remove this comment)
(script# 906)
(include sci.sh)

; Family 6: an empty for loop ends a while body.
; The inner exit folds to the outer head. A dead back-jump follows the inner
; latch. The decompiler prunes the dead jump. The follow-node search then
; targets an address with no node.
(public
	f6EmptyTrailingFor 0
)

(procedure (f6EmptyTrailingFor &tmp temp0 temp1)
	(asm
	outerHead:
		lat temp0
		ldi 100
		lt?
		bnt outerExit
		ldi 0
		sat temp1
	innerHead:
		lat temp1
		ldi 70
		lt?
		bnt outerHead
		lat temp1
		ldi 1
		add
		sat temp1
		jmp innerHead
		jmp outerHead
	outerExit:
		ret
	)
)
