;;; Sierra Script 1.0 - (do not remove this comment)
(script# 966)
(include sci.sh)

; The optimiser turns a push of a number that the accumulator holds into
; "push". The push of the argument count of a call, or of a selector, can
; then take a store whose value is that number (SQ1 VGA script 34,
; egoDropOratPart::changeState; KQ6 floppy script 370, AzurePrint::init).
; A selector can also be the value of a variable that a store sets (Hoyle
; Classic script 700, BridgeHand::bid). The argument count of a send can read
; back a variable that a store of a number set.
; Sierra: (= temp0 2) (Random 2 1) (= temp1 init) (param1 init:)
; (= temp2 (param1 size:)) (param1 temp2:) (= temp0 3) (param1 posn: 1 2 3).
(public
	v3StoreInSlot 0
)

(procedure (v3StoreInSlot param1 &tmp temp0 temp1 temp2)
	(asm
		ldi 2
		sat temp0
		push
		push
		push1
		callk Random, 4
		ldi #init
		sat temp1
		push
		push0
		lap param1
		send 4
		pushi #size
		push0
		lap param1
		send 4
		sat temp2
		push
		push0
		lap param1
		send 4
		ldi 3
		sat temp0
		pushi #posn
		push
		pushi 1
		pushi 2
		pushi 3
		lap param1
		send 10
		ret
	)
)
