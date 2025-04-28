import sqlite3
from typing import List, Dict

class ExperimentRAG:
    def __init__(self, db_path: str = 'funsearch.db'):
        """Initialize the RAG system with database connection"""
        self.conn = sqlite3.connect(db_path)
        self.cursor = self.conn.cursor()
    
    def get_top_policies_by_cache_hit(self, workload: str, top_n: int = 2) -> List[Dict]:
        """
        Retrieve top N policies for a given workload based on cache hit rate
        
        Args:
            workload: The workload to query
            top_n: Number of top policies to return (default: 2)
            
        Returns:
            List of dictionaries containing policy information with:
            - policy name
            - policy description
            - workload description
            - cpp file path
            - cache hit rate
        """
        query = '''
        SELECT 
            policy, 
            policy_description, 
            workload_description, 
            cpp_file_path,
            cache_hit_rate
        FROM experiments
        WHERE workload = ?
        ORDER BY cache_hit_rate DESC
        LIMIT ?
        '''
        
        self.cursor.execute(query, (workload, top_n))
        results = self.cursor.fetchall()
        
        # Convert results to list of dictionaries
        policies = []
        for row in results:
            policies.append({
                'policy': row[0],
                'policy_description': row[1],
                'workload_description': row[2],
                'cpp_file_path': row[3],
                'cache_hit_rate': row[4]
            })
        
        return policies
    
    def generate_response(self, workload: str) -> str:
        """
        Generate a natural language response with the top policies for a workload
        
        Args:
            workload: The workload to query
            
        Returns:
            Formatted response string
        """
        top_policies = self.get_top_policies_by_cache_hit(workload)
        
        if not top_policies:
            return f"No data available for workload: {workload}"
        
        response = f"Workload: {workload}\n"
        response += f"Description: {top_policies[0]['workload_description']}\n\n"
        response += f"Top {len(top_policies)} policies by cache hit rate:\n\n"
        
        for i, policy in enumerate(top_policies, 1):
            response += f"{i}. Policy: {policy['policy']}\n"
            response += f"   Description: {policy['policy_description']}\n"
            response += f"   Cache Hit Rate: {policy['cache_hit_rate']:.2%}\n"
            response += f"   CPP File Path: {policy['cpp_file_path']}\n\n"
        
        return response
    
    def close(self):
        """Close database connection"""
        self.conn.close()

# Example usage
if __name__ == "__main__":
    rag = ExperimentRAG()
    
    # Example query for a specific workload
    workload_query = "example_workload"  # Replace with your actual workload
    response = rag.generate_response(workload_query)
    print(response)
    
    rag.close()