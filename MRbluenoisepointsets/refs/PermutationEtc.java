import java.util.Arrays;

/**
 * An improvement on the performance of http://extremelearning.com.au/isotropic-blue-noise-point-sets/
 */
public class PermutationEtc {

	/**
	 * Change SIZE to increase the size of the balanced permutations.
	 */
	public static final int SIZE = 52, HALF_SIZE = SIZE >>> 1, COUNT = 100;

	public PermutationEtc(){}
	public PermutationEtc(long state){
		stateA = state;
	}
	/**
	 * Can be any long.
	 */
	private long stateA = 12345678987654321L; //System.nanoTime();
	/**
	 * Must be odd.
	 */
	private long stateB = 0x1337DEADBEEFL;

	/**
	 * It's a weird RNG. Returns a slightly-biased pseudo-random int between 0 inclusive and bound exclusive. 
	 * The bias comes from not completely implementing Daniel Lemire's fastrange algorithm, but it should only 
	 * be relevant for huge bounds. The number generator itself passes PractRand without anomalies, has a state 
	 * size of 127 bits, and a period of 2 to the 127.
	 * @param bound upper exclusive bound
	 * @return an int between 0 (inclusive) and bound (exclusive)
	 */
	private int nextIntBounded (int bound) {
		final long s = (stateA += 0xC6BC279692B5C323L);
		final long z = ((s < 0x800000006F17146DL) ? stateB : (stateB += 0x9479D2858AF899E6L)) * (s ^ s >>> 31);
		return (int)(bound * ((z ^ z >>> 25) & 0xFFFFFFFFL) >>> 32);
	}

	private static void swap(int[] arr, int pos1, int pos2) {
		final int tmp = arr[pos1];
		arr[pos1] = arr[pos2];
		arr[pos2] = tmp;
	}

	/**
	 * Fisher-Yates and/or Knuth shuffle, done in-place on an int array.
	 * @param elements will be modified in-place by a relatively fair shuffle
	 */
	public void shuffleInPlace(int[] elements) {
		final int size = elements.length;
		for (int i = size; i > 1; i--) {
			swap(elements, i - 1, nextIntBounded(i));
		}
	}

	public static void main(String[] args)
	{
		//PermutationEtc p = new PermutationEtc(System.nanoTime());
		PermutationEtc p = new PermutationEtc();

		final int[][] allItems = new int[COUNT][SIZE];
		final int[] delta = new int[SIZE], targets = new int[SIZE];
		final int LIMIT = 10000 * COUNT * COUNT;

/*
		int[] items = allItems[1];
		System.out.println("Array.length: "+items.length);
		for(int x=0;x<items.length;x++)
		{
			System.out.println(items[x]);
		}
*/

/*
		for(int c=0;c<COUNT;c++) {
			int[] items = allItems[c];
			for(int d=0;d<SIZE;d++){
				items[d] = d*c;
			}
		}

		for(int x=0;x<COUNT;x++){
			System.out.println("");
			for(int y=0;y<SIZE;y++)
			System.out.println(allItems[x][y]);
		}
*/


		long startTime = System.currentTimeMillis();
		for (int g = 0; g < COUNT; g++) {
			int[] items = allItems[g];
			// this is limited to 10000 * COUNT * COUNT runs as an emergency stop 
			// in case we run out of distinct permutations
			BIG_LOOP:
			for (int repeat = 0; repeat < LIMIT; repeat++)
			{
				for (int i = 0; i < HALF_SIZE; i++) {
					delta[i] = i + 1;
					delta[i + HALF_SIZE] = ~i;
				}
				p.shuffleInPlace(delta);
				for (int i = 0; i < SIZE; i++) {
					targets[i] = i + 1; 
				}
				targets[(items[0] = p.nextIntBounded(SIZE) + 1) - 1] = 0;
				for (int i = 1; i < SIZE; i++) {
					int d = 0;
					for (int j = 0; j < SIZE; j++) {
						d = delta[j];
						if(d == 0) continue;
						int t = items[i-1] + d;
						if(t > 0 && t <= SIZE && targets[t-1] != 0){
							items[i] = t;
							targets[t-1] = 0;
							delta[j] = 0;
							break;
						}
						else d = 0;
					}
					if(d == 0) continue BIG_LOOP;
				}
				int d = items[0] - items[SIZE - 1];
				System.out.println(g+1);
				for (int j = 0; j < SIZE; j++) {
					if(d == delta[j]) {
						// distinct array check
						for (int i = 0; i < g; i++) {
							if(!Arrays.equals(allItems[i], items)) {
							/*	int[] xxx = allItems[i];
								for(int k=0;k<SIZE;k++){
									System.out.print(xxx[k]+", ");
								}
								System.out.println("");
								for(int k=0;k<SIZE;k++){
									System.out.print(items[k]+", ");
								}
							*/
							}else continue BIG_LOOP;
						}
						System.out.print(items[0]);
						for (int i = 1; i < SIZE; i++) {
							System.out.print(", " + items[i]);
						}
						System.out.println();
						break BIG_LOOP;
					}
				}
			}
		}
		System.out.println("Took " + (System.currentTimeMillis() - startTime) * 0.001 +
			" seconds to generate 100 sequences with size " + SIZE);

	}
}
