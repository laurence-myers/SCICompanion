;;; Sierra Script 1.0 - (do not remove this comment)
(script# 938)
(include sci.sh)

; A while that is the first statement of a repeat, so the two loops share
; their head, with no break in the while. As one loop this structures (a
; cond in the repeat), so the decompiler keeps that text: it builds nested
; loops only for a function that does not structure as one loop.
(public
	f16SharedHeadOneLoop 0
)

(procedure (f16SharedHeadOneLoop &tmp temp0 temp1)
	(asm
	head:
		lst temp0
		ldi 10
		lt?
		bnt whileDone
		+at temp0
		jmp head
	whileDone:
		lst temp1
		ldi 5
		gt?
		bt done
		+at temp1
		jmp head
	done:
		ret
	)
)
