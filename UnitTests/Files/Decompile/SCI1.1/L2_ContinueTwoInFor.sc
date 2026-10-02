;;; Sierra Script 1.0 - (do not remove this comment)
(script# 962)
(include sci.sh)

; A continue of the outer loop, a for, from an inner while: it goes to the
; step of the for. Shape of Hoyle 3, script 100 (DominoHand::setNextPosn).
(public
	l2ContinueTwoInFor 0
)

(procedure (l2ContinueTwoInFor param1 &tmp temp0 temp1)
	(asm
		ldi 0
		sat temp0
	outer:
		lst temp0
		lap param1
		lt?
		bnt done
		ldi 0
		sat temp1
	inner:
		lst temp1
		ldi 5
		lt?
		bnt innerDone
		lst temp1
		lat temp0
		eq?
		bnt next
		jmp step
	next:
		+at temp1
		jmp inner
	innerDone:
		ldi 9
		sat temp1
	step:
		+at temp0
		jmp outer
	done:
		ret
	)
)
