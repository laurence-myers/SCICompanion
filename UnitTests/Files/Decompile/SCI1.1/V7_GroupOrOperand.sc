;;; Sierra Script 1.0 - (do not remove this comment)
(script# 957)
(include sci.sh)

; The second operand of an or with a statement before its value, where an
; instruction after the or reads its value (QfG1 VGA script 0, proc0_3: a
; store, then a loop whose value the or takes, and the bnt of an if after
; the or). The second operand is a group.
; Sierra: (if (or (not param1) (not (for ((= temp0 17)) (<= temp0 24) ((++ temp0)) (if (== temp0 param1) (return 1) (Random))))) (= temp0 2)) (return 0)
; In the second procedure, no instruction reads the value of the or: it
; is a statement, (if (not c) X).
(public
	v7GroupOrOperand 0
	v7GroupOrStatement 1
)

(procedure (v7GroupOrOperand param1 &tmp temp0)
	(asm
		lap param1
		not
		bt test
		ldi 17
		sat temp0
	head:
		lst temp0
		ldi 24
		le?
		bnt exit
		lst temp0
		lap param1
		eq?
		bnt next
		ldi 1
		ret
		pushi 0
		callk Random, 0
	next:
		+at temp0
		jmp head
	exit:
		not
	test:
		bnt done
		ldi 2
		sat temp0
	done:
		ldi 0
		ret
	)
)

(procedure (v7GroupOrStatement param1 &tmp temp0)
	(asm
		lap param1
		bt end
		ldi 17
		sat temp0
		pushi 0
		callk Random, 0
	end:
		ldi 2
		sat temp0
		ret
	)
)
