

byte nodeID = 0;
byte pathID;
byte RSSI_C;
byte LQI_C;
byte hopCount;

struct msg {
	byte nodeID;
	byte pathID;
	byte hopCount;
	word powerLVL;
}




msg node_init() {
	


	return node;
}

fsm root {

	struct msg * payload;
	lword time;
	address packet;
	word p1, tr;
	byte RSSI, LQI;
	int count = 0;

	state Init:

	state Init_t:
		tcv_control (sfd, PHYSOPT_SETPOWER, &power);
		tcv_control (sfd, PHYSOPT_GETPOWER, &ReadPower);
			
		

	state Sending:

		//Sets timer
		time = seconds();
		packet = tcv_wnp(sending, sfd, 9);
		packet[0] = 0;

		char * p (char *)(packet+1);
		*p = payload->nodeID;p++;
		*p = payload->pathID;p++;
		*p = payload->hopCount;p++;
		*p = payload->powerLVL;p++;

		tcv_endp_(packet);
		ufree(payload);

	state Receive_Connection:

		//RSSI is check by potential parents
		packet = tcv_rnp(Receiving_Connection, sfd);
		
		//Checks timer
		if((seconds()-time) > 90){
			proceed Power_UP; 
		}

	state Measuring:

		p1 = (tcv_left(packet))>>1;
		tr = packet[p1-1];
		RSSI = (byte) (tr>>8);
		LQI = (byte) tr;

	state Update:
		//Parent is set to the node id of the message 
		//So the node id will always be id of the node who
		//sent the message
		struct msg* payload = (struct msg*)(packet + 1);
		
		//checks for multiple connections
		count ++;
		if(count > 1){
			leds_all(0);
			led(3, 1);
			// If distance is short look for another
			// connection
			if((payload->hopCount + 1) > hopCount){
				proceed Receive_Connection:
			}
		}
		//updates if better connection
		RSSI_C = RSSI;
		LQI_C = LQI;
		hopCount = payload->hopCount + 1;
		pathID = payload->nodeID;

		tcv_endp(packet);
		proceed Receive_Connection;

	//increments power until max and if max sends to shutdown.
	state Power_Up:
		word power;
		tcv_control (sfd, PHYSOPT_GETPOWER,(&power));
		if(power == 7){
			proceed Shut_Down;
		}

		tcv_control (sfd, PHYSOPT_SETPOWER,(&power + 1));
		proceed Sending;	

	state Shut_Down:
		leds_all(0);
		finish;

}

fsm additional_send {

		
}

fsm recieve {
	address packet;
		
	state Receiving:
		packet = tcv_rnp(Receiving,sfd);

	state OK:
		struct msg* payload = (struct msg*)(packet+1);

		//switch statement
			
			//case: parent node->child nodes
			
			//case: child node->parent node->sink node

			//case: new node -> new connection				

}





