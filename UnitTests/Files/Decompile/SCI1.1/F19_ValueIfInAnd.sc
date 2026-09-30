;;; Sierra Script 1.0 - (do not remove this comment)
(script# 943)
(include sci.sh)

; An operand of an and compares a send with an if used as a value, and the
; if has an else. The join of that if is reached by the then's "jmp" and the
; else's fall-through, and its compare needs the value of the if. The and
; must not be built at that join before the if is built. The compare then
; takes its first operand, the push of the send, from before the if.
; Shape from rm21::doit and rm22::doit of King's Quest I SCI.
; f19ValueIfInOr: the compare is the first operand of an or, so the join of
; the if ends in the or's "bt". No if may be built there before the value
; if. Shape from Gaza::doit (script 107) of Conquests of Camelot.
; f19StoredValueIf: the join of the if stores the value, and a second if
; tests it again. The if at the join is built first, as for any statement.
; Shape from Room49::init (script 49) of King's Quest IV.
; f19StoredValueIfAnd: the same, but the second if tests an and whose first
; operand is the stored value. The and starts at the join, as for any
; statement.
; f19PushedValueIf: the join of the if pushes its value for a later
; operator, (== (+ (if a 13 else 0) 61) b) inside an and. Shape from
; partTwo::changeState (script 350) of The Island of Dr. Brain.
(public
	f19ValueIfInAnd 0
	f19ValueIfInOr 1
	f19StoredValueIf 2
	f19StoredValueIfAnd 3
	f19PushedValueIf 4
)

(procedure (f19ValueIfInAnd param1 param2 param3 &tmp temp0)
	(asm
		lap param1
		bnt other
		lsp param2
		ldi 2
		ne?
		bnt other
		pushi #view
		pushi 0
		lap param3
		send 4
		push
		pushi 1
		pushi 0
		callk Random, 2
		bnt elseArm
		ldi 23
		jmp join
	elseArm:
		ldi 16
	join:
		ne?
		bnt other
		pushi 1
		pushi 1
		callk Random, 2
		not
		bnt other
		ldi 5
		sat temp0
		jmp done
	other:
		ldi 6
		sat temp0
	done:
		ret
	)
)

(procedure (f19ValueIfInOr param1 param2 param3 &tmp temp0)
	(asm
		lap param3
		bnt else3
		lat temp0
		not
		bnt else3
		pushi #loop
		pushi 0
		lap param1
		send 4
		push
		lsp param2
		ldi 48
		eq?
		bnt elseArm3
		ldi 1
		jmp join3
	elseArm3:
		ldi 0
	join3:
		eq?
		bt thenPart
		pushi #loop
		pushi 0
		lap param1
		send 4
		push
		ldi 3
		eq?
	thenPart:
		bnt else3
		ldi 5
		sat temp0
		jmp done3
	else3:
		ldi 6
		sat temp0
	done3:
		ret
	)
)

(procedure (f19StoredValueIf param1 param2 param3 &tmp temp0)
	(asm
		lsp param1
		ldi 1
		gt?
		bnt elseArm4
		ldi 0
		jmp join4
	elseArm4:
		ldi 1
	join4:
		sat temp0
		pushi #cel
		pushi 1
		bnt else5
		pushi #lastCel
		pushi 0
		lap param2
		send 4
		jmp join5
	else5:
		ldi 0
	join5:
		push
		pushi #init
		pushi 0
		lap param2
		send 10
		ret
	)
)

(procedure (f19StoredValueIfAnd param1 param2 param3 &tmp temp0)
	(asm
		lsp param1
		ldi 1
		gt?
		bnt elseArm6
		ldi 0
		jmp join6
	elseArm6:
		ldi 1
	join6:
		sat temp0
		pushi #cel
		pushi 1
		bnt else7
		lap param3
		bnt else7
		pushi #lastCel
		pushi 0
		lap param2
		send 4
		jmp join7
	else7:
		ldi 0
	join7:
		push
		pushi #init
		pushi 0
		lap param2
		send 10
		ret
	)
)

(procedure (f19PushedValueIf param1 param2 param3 &tmp temp0)
	(asm
		lap param1
		bnt other8
		lap param2
		bnt elseArm8
		ldi 13
		jmp join8
	elseArm8:
		ldi 0
	join8:
		push
		ldi 61
		add
		push
		lap param3
		eq?
		bnt other8
		lat temp0
		bnt other8
		ldi 5
		sat temp0
		jmp done8
	other8:
		ldi 6
		sat temp0
	done8:
		ret
	)
)