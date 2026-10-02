;;; Sierra Script 1.0 - (do not remove this comment)
(script# 906)
(include sci.sh)

; Family 6: an empty for loop ends a while body. The exit of the inner loop
; goes to the head of the outer loop, and a dead back-jump follows the latch
; of the inner loop.
(public
	f6EmptyTrailingFor 0
)

(procedure (f6EmptyTrailingFor &tmp temp0 temp1)
	(asm
	outerHead:
		lst temp0
		ldi 100
		lt?
		bnt outerExit
		ldi 0
		sat temp1
	innerHead:
		lst temp1
		ldi 70
		lt?
		bnt outerHead
		lst temp1
		ldi 1
		add
		sat temp1
		jmp innerHead
		jmp outerHead
	outerExit:
		ret
	)
)
