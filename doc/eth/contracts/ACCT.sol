pragma solidity ^0.4.15;

/** Atomic Cross Chain Transfer
 *
 * Case 1: A wants to exchange some ETH against some BTC with B:
 *
 *  - A creates a secret X (up to 32 bytes) and hashes it H(X) (with the ripemd160 algorithm)
 *  - A calls `initiate()` and sends some ETH to the smart contract, specifing H(X) and the
 *    time when A can get back its coins
 *  - B gets notified via the `Initiated` event and verifies it is happy with: (1) the amount of
 *    coins A sent to the smart contract, (2) the time after which A can get back its coins
 *  - TODO
 *
 */
contract ACCT {
    enum SwapType { None, Initiated, Responded }

    struct Swap {
        SwapType swapType;
        address initiator;
        address responder;
        uint256 value;
        uint256 refundTimePoint;
    }

    mapping(bytes20 => Swap) public swaps;

    /** Event: An atomic swap has been initiated
     *
     * \param hashedSecret    Hash of initiator's secret (hash algorithm is ripemd160)
     * \param initiator       Address of the initiator
     * \param responder       Address of the responder (who will get the coins)
     * \param value           How many wei will change hands
     * \param refundTimePoint From when the initiator can get its coins back
     */
    event Initiated(
        bytes20 hashedSecret,
        address initiator,
        address responder,
        uint256 value,
        uint256 refundTimePoint
    );

    /** Event: An atomic swap has been responded to
     *
     * \param hashedSecret    Hash of initiator's secret (hash algorithm is ripemd160)
     * \param initiator       Address of the initiator (who will get the coins)
     * \param responder       Address of the responder
     * \param value           How many wei will change hands
     * \param refundTimePoint From when the responder can get its coins back
     */
    event Responded(
        bytes20 hashedSecret,
        address initiator,
        address responder,
        uint256 value,
        uint256 refundTimePoint
    );

    /** Event: Whomever put money into the swap took its coins back
     *
     * \param hashedSecret Hash of initiator's secret (used to identify the swap)
     * \param receipient   Address where the coins where sent back
     * \param value        How many wei were sent
     */
    event Refunded(bytes20 hashedSecret, address recipient, uint256 value);

    /** Event: Peer redeemed the coins
     *
     * \param hashedSecret Hash of initiator's secret (used to identify the swap)
     * \param receipient   Address where the coins where sent
     * \param value        How many wei were sent
     */
    event Redeemed(bytes20 hashedSecret, address recipient, uint256 value);

    /** Check that the swap identified by `hashedSecret` is empty */
    modifier isEmpty(bytes20 hashedSecret) {
        require(swaps[hashedSecret].swapType == SwapType.None);
        _;
    }

    /** Check that the swap identified by `hashedSecret` can be refunded to the sender */
    modifier isRefundable(bytes20 hashedSecret) {
        var swap = swaps[hashedSecret];
        require(swap.swapType != SwapType.None);
        if (swap.swapType == SwapType.Initiated) {
            require(msg.sender == swap.initiator);
        } else {
            require(msg.sender == swap.responder);
        }
        require(now > swap.refundTimePoint);
        _;
    }

    /** Check that the swap identified by `hashedSecret` can be redeemed to the sender */
    modifier isRedeemable(bytes20 hashedSecret, bytes secret) {
        require(ripemd160(secret) == hashedSecret);
        var swap = swaps[hashedSecret];
        require(swap.swapType != SwapType.None);
        if (swap.swapType == SwapType.Initiated) {
            require(msg.sender == swap.responder);
        } else {
            require(msg.sender == swap.initiator);
        }
        require(now < swap.refundTimePoint);
        _;
    }

    /** Initiate an atomic swap to another blockchain
     *
     * \param hashedSecret   Hash of the initiator's secret
     * \param responder      Address of the responder on this blockchain
     * \param escrowDuration Escrow period, in seconds (from now)
     */
    function initiate(
        bytes20 hashedSecret,
        address responder,
        uint256 escrowDuration
    )
        public
        payable
        isEmpty(hashedSecret)
    {
        require(msg.value > 0);
        var swap = swaps[hashedSecret];
        swap.swapType = SwapType.Initiated;
        swap.initiator = msg.sender;
        swap.responder = responder;
        swap.value = msg.value;
        swap.refundTimePoint = now + escrowDuration;
        Initiated(hashedSecret, swap.initiator, swap.responder, swap.value, swap.refundTimePoint);
    }

    /** Repond to an atomic swap from another blockchain
     *
     * \param hashedSecret   Hash of the initiator's secret
     * \param initiator      Address of the initiator on this blockchain
     * \param escrowDuration Escrow period, in seconds (from now)
     */
    function respond(
        bytes20 hashedSecret,
        address initiator,
        uint256 escrowDuration
    )
        public
        payable
        isEmpty(hashedSecret)
    {
        require(msg.value > 0);
        var swap = swaps[hashedSecret];
        swap.swapType = SwapType.Responded;
        swap.initiator = initiator;
        swap.responder = msg.sender;
        swap.value = msg.value;
        swap.refundTimePoint = now + escrowDuration;
        Responded(hashedSecret, swap.initiator, swap.responder, swap.value, swap.refundTimePoint);
    }

    /** The initiator/responder wants its coins back
     *
     * \param hashedSecret Hash of initiator's secret
     */
    function refund(bytes20 hashedSecret) public isRefundable(hashedSecret) {
        var swap = swaps[hashedSecret];
        uint256 value = swap.value;
        swap.value = 0;
        swap.swapType = SwapType.None;
        msg.sender.transfer(value);
        Refunded(hashedSecret, msg.sender, value);
    }

    /** The initiator/responder wants to execute the deal
     *
     * \param hashedSecret Hash of initiator's secret
     * \param secret       Initiator's secret
     */
    function redeem(bytes20 hashedSecret, bytes secret) public isRedeemable(hashedSecret, secret) {
        var swap = swaps[hashedSecret];
        uint256 value = swap.value;
        swap.value = 0;
        swap.swapType = SwapType.None;
        msg.sender.transfer(value);
        Redeemed(hashedSecret, msg.sender, value);
    }
}
